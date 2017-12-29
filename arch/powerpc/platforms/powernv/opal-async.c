/*
 * PowerNV OPAL asynchronous completion interfaces
 *
 * Copyright 2013-2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/gfp.h>
#include <linux/of.h>
#include <asm/machdep.h>
#include <asm/opal.h>

enum opal_async_token_state {
	ASYNC_TOKEN_UNALLOCATED = 0,
	ASYNC_TOKEN_ALLOCATED,
	ASYNC_TOKEN_DISPATCHED,
	ASYNC_TOKEN_ABANDONED,
	ASYNC_TOKEN_COMPLETED
};

struct opal_async_token {
	enum opal_async_token_state state;
	struct opal_msg response;
};

static DECLARE_WAIT_QUEUE_HEAD(opal_async_wait);
static DEFINE_SPINLOCK(opal_async_comp_lock);
static struct semaphore opal_async_sem;
static unsigned int opal_max_async_tokens;
static struct opal_async_token *opal_async_tokens;

static int __opal_async_get_token(void)
{
	unsigned long flags;
	int i, token = -EBUSY;

	spin_lock_irqsave(&opal_async_comp_lock, flags);

	for (i = 0; i < opal_max_async_tokens; i++) {
		if (opal_async_tokens[i].state == ASYNC_TOKEN_UNALLOCATED) {
			opal_async_tokens[i].state = ASYNC_TOKEN_ALLOCATED;
			token = i;
			break;
		}
	}

	spin_unlock_irqrestore(&opal_async_comp_lock, flags);
	return token;
}

/*
 * Note: If the returned token is used in an opal call and opal returns
 * OPAL_ASYNC_COMPLETION you MUST call one of opal_async_wait_response() or
 * opal_async_wait_response_interruptible() at least once before calling another
 * opal_async_* function
 */
int opal_async_get_token_interruptible(void)
{
	int token;

	/* Wait until a token is available */
	if (down_interruptible(&opal_async_sem))
		return -ERESTARTSYS;

	token = __opal_async_get_token();
	if (token < 0)
		up(&opal_async_sem);

	return token;
}
EXPORT_SYMBOL_GPL(opal_async_get_token_interruptible);

static int __opal_async_release_token(int token)
{
	unsigned long flags;
	int rc;

	if (token < 0 || token >= opal_max_async_tokens) {
		pr_err("%s: Passed token is out of range, token %d\n",
				__func__, token);
		return -EINVAL;
	}

	spin_lock_irqsave(&opal_async_comp_lock, flags);
	switch (opal_async_tokens[token].state) {
	case ASYNC_TOKEN_COMPLETED:
	case ASYNC_TOKEN_ALLOCATED:
		opal_async_tokens[token].state = ASYNC_TOKEN_UNALLOCATED;
		rc = 0;
		break;
	/*
	 * DISPATCHED and ABANDONED tokens must wait for OPAL to respond.
	 * Mark a DISPATCHED token as ABANDONED so that the response handling
	 * code knows no one cares and that it can free it then.
	 */
	case ASYNC_TOKEN_DISPATCHED:
		opal_async_tokens[token].state = ASYNC_TOKEN_ABANDONED;
		/* Fall through */
	default:
		rc = 1;
	}
	spin_unlock_irqrestore(&opal_async_comp_lock, flags);

	return rc;
}

int opal_async_release_token(int token)
{
	int ret;

	ret = __opal_async_release_token(token);
	if (!ret)
		up(&opal_async_sem);

	return ret;
}
EXPORT_SYMBOL_GPL(opal_async_release_token);

int opal_async_wait_response(uint64_t token, struct opal_msg *msg)
{
	if (token >= opal_max_async_tokens) {
		pr_err("%s: Invalid token passed\n", __func__);
		return -EINVAL;
	}

	if (!msg) {
		pr_err("%s: Invalid message pointer passed\n", __func__);
		return -EINVAL;
	}

	/*
	 * There is no need to mark the token as dispatched, wait_event()
	 * will block until the token completes.
	 *
	 * Wakeup the poller before we wait for events to speed things
	 * up on platforms or simulators where the interrupts aren't
	 * functional.
	 */
	opal_wake_poller();
	wait_event(opal_async_wait, opal_async_tokens[token].state
			== ASYNC_TOKEN_COMPLETED);
	memcpy(msg, &opal_async_tokens[token].response, sizeof(*msg));

	return 0;
}
EXPORT_SYMBOL_GPL(opal_async_wait_response);

int opal_async_wait_response_interruptible(uint64_t token, struct opal_msg *msg)
{
	unsigned long flags;
	int ret;

	if (token >= opal_max_async_tokens) {
		pr_err("%s: Invalid token passed\n", __func__);
		return -EINVAL;
	}

	if (!msg) {
		pr_err("%s: Invalid message pointer passed\n", __func__);
		return -EINVAL;
	}

	/*
	 * The first time this gets called we mark the token as DISPATCHED
	 * so that if wait_event_interruptible() returns not zero and the
	 * caller frees the token, we know not to actually free the token
	 * until the response comes.
	 *
	 * Only change if the token is ALLOCATED - it may have been
	 * completed even before the caller gets around to calling this
	 * the first time.
	 *
	 * There is also a dirty great comment at the token allocation
	 * function that if the opal call returns OPAL_ASYNC_COMPLETION to
	 * the caller then the caller *must* call this or the not
	 * interruptible version before doing anything else with the
	 * token.
	 */
	if (opal_async_tokens[token].state == ASYNC_TOKEN_ALLOCATED) {
		spin_lock_irqsave(&opal_async_comp_lock, flags);
		if (opal_async_tokens[token].state == ASYNC_TOKEN_ALLOCATED)
			opal_async_tokens[token].state = ASYNC_TOKEN_DISPATCHED;
		spin_unlock_irqrestore(&opal_async_comp_lock, flags);
	}

	/*
	 * Wakeup the poller before we wait for events to speed things
	 * up on platforms or simulators where the interrupts aren't
	 * functional.
	 */
	opal_wake_poller();
	ret = wait_event_interruptible(opal_async_wait,
			opal_async_tokens[token].state ==
			ASYNC_TOKEN_COMPLETED);
	if (!ret)
		memcpy(msg, &opal_async_tokens[token].response, sizeof(*msg));

	return ret;
}
EXPORT_SYMBOL_GPL(opal_async_wait_response_interruptible);

/* Called from interrupt context */
static int opal_async_comp_event(struct notifier_block *nb,
		unsigned long msg_type, void *msg)
{
	struct opal_msg *comp_msg = msg;
	enum opal_async_token_state state;
	unsigned long flags;
	uint64_t token;

	if (msg_type != OPAL_MSG_ASYNC_COMP)
		return 0;

	token = be64_to_cpu(comp_msg->params[0]);
	spin_lock_irqsave(&opal_async_comp_lock, flags);
	state = opal_async_tokens[token].state;
	opal_async_tokens[token].state = ASYNC_TOKEN_COMPLETED;
	spin_unlock_irqrestore(&opal_async_comp_lock, flags);

	if (state == ASYNC_TOKEN_ABANDONED) {
		/* Free the token, no one else will */
		opal_async_release_token(token);
		return 0;
	}
	memcpy(&opal_async_tokens[token].response, comp_msg, sizeof(*comp_msg));
	wake_up(&opal_async_wait);

	return 0;
}

static struct notifier_block opal_async_comp_nb = {
		.notifier_call	= opal_async_comp_event,
		.next		= NULL,
		.priority	= 0,
};

int __init opal_async_comp_init(void)
{
	struct device_node *opal_node;
	const __be32 *async;
	int err;

	opal_node = of_find_node_by_path("/ibm,opal");
	if (!opal_node) {
		pr_err("%s: Opal node not found\n", __func__);
		err = -ENOENT;
		goto out;
	}

	async = of_get_property(opal_node, "opal-msg-async-num", NULL);
	if (!async) {
		pr_err("%s: %pOF has no opal-msg-async-num\n",
				__func__, opal_node);
		err = -ENOENT;
		goto out_opal_node;
	}

	opal_max_async_tokens = be32_to_cpup(async);
	opal_async_tokens = kcalloc(opal_max_async_tokens,
			sizeof(*opal_async_tokens), GFP_KERNEL);
	if (!opal_async_tokens) {
		err = -ENOMEM;
		goto out_opal_node;
	}

	err = opal_message_notifier_register(OPAL_MSG_ASYNC_COMP,
			&opal_async_comp_nb);
	if (err) {
		pr_err("%s: Can't register OPAL event notifier (%d)\n",
				__func__, err);
		kfree(opal_async_tokens);
		goto out_opal_node;
	}

	sema_init(&opal_async_sem, opal_max_async_tokens);

out_opal_node:
	of_node_put(opal_node);
out:
	return err;
}
