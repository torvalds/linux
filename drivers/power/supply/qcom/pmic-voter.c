// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2017, 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/pmic-voter.h>

#define NUM_MAX_CLIENTS		32
#define DEBUG_FORCE_CLIENT	"DEBUG_FORCE_CLIENT"

static DEFINE_SPINLOCK(votable_list_slock);
static LIST_HEAD(votable_list);

static struct dentry *debug_root;

struct client_vote {
	bool	enabled;
	int	value;
};

struct votable {
	const char		*name;
	const char		*override_client;
	struct list_head	list;
	struct client_vote	votes[NUM_MAX_CLIENTS];
	int			num_clients;
	int			type;
	int			effective_client_id;
	int			effective_result;
	int			override_result;
	struct mutex		vote_lock;
	void			*data;
	int			(*callback)(struct votable *votable,
						void *data,
						int effective_result,
						const char *effective_client);
	char			*client_strs[NUM_MAX_CLIENTS];
	bool			voted_on;
	struct dentry		*root;
	struct dentry		*status_ent;
	u32			force_val;
	struct dentry		*force_val_ent;
	bool			force_active;
	struct dentry		*force_active_ent;
};

/**
 * vote_set_any()
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets 0 or 1 based on the voting
 * @eff_id:	Always returns the client_id argument
 *
 * Note that for SET_ANY voter, the value is always same as enabled. There is
 * no idea of a voter abstaining from the election. Hence there is never a
 * situation when the effective_id will be invalid, during election.
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void vote_set_any(struct votable *votable, int client_id,
				int *eff_res, int *eff_id)
{
	int i;

	*eff_res = 0;

	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++)
		*eff_res |= votable->votes[i].enabled;

	*eff_id = client_id;
}

/**
 * vote_min() -
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets this to the min. of all the values amongst enabled voters.
 *		If there is no enabled client, this is set to INT_MAX
 * @eff_id:	sets this to the client id that has the min value amongst all
 *		the enabled clients. If there is no enabled client, sets this
 *		to -EINVAL
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void vote_min(struct votable *votable, int client_id,
				int *eff_res, int *eff_id)
{
	int i;

	*eff_res = INT_MAX;
	*eff_id = -EINVAL;
	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		if (votable->votes[i].enabled
			&& *eff_res > votable->votes[i].value) {
			*eff_res = votable->votes[i].value;
			*eff_id = i;
		}
	}
	if (*eff_id == -EINVAL)
		*eff_res = -EINVAL;
}

/**
 * vote_max() -
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets this to the max. of all the values amongst enabled voters.
 *		If there is no enabled client, this is set to -EINVAL
 * @eff_id:	sets this to the client id that has the max value amongst all
 *		the enabled clients. If there is no enabled client, sets this to
 *		-EINVAL
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void vote_max(struct votable *votable, int client_id,
				int *eff_res, int *eff_id)
{
	int i;

	*eff_res = INT_MIN;
	*eff_id = -EINVAL;
	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		if (votable->votes[i].enabled &&
				*eff_res < votable->votes[i].value) {
			*eff_res = votable->votes[i].value;
			*eff_id = i;
		}
	}
	if (*eff_id == -EINVAL)
		*eff_res = -EINVAL;
}

static int get_client_id(struct votable *votable, const char *client_str)
{
	int i;

	for (i = 0; i < votable->num_clients; i++) {
		if (votable->client_strs[i]
		 && (strcmp(votable->client_strs[i], client_str) == 0))
			return i;
	}

	/* new client */
	for (i = 0; i < votable->num_clients; i++) {
		if (!votable->client_strs[i]) {
			votable->client_strs[i]
				= kstrdup(client_str, GFP_KERNEL);
			if (!votable->client_strs[i])
				return -ENOMEM;
			return i;
		}
	}
	return -EINVAL;
}

static char *get_client_str(struct votable *votable, int client_id)
{
	if (!votable || (client_id == -EINVAL))
		return NULL;

	return votable->client_strs[client_id];
}

void lock_votable(struct votable *votable)
{
	mutex_lock(&votable->vote_lock);
}
EXPORT_SYMBOL_GPL(lock_votable);

void unlock_votable(struct votable *votable)
{
	mutex_unlock(&votable->vote_lock);
}
EXPORT_SYMBOL_GPL(unlock_votable);

/**
 * is_override_vote_enabled() -
 * is_override_vote_enabled_locked() -
 *		The unlocked and locked variants of getting whether override
		vote is enabled.
 * @votable:	the votable object
 *
 * Returns:
 *	True if the client's vote is enabled; false otherwise.
 */
bool is_override_vote_enabled_locked(struct votable *votable)
{
	if (!votable)
		return false;

	return votable->override_result != -EINVAL;
}
EXPORT_SYMBOL_GPL(is_override_vote_enabled_locked);

bool is_override_vote_enabled(struct votable *votable)
{
	bool enable;

	if (!votable)
		return false;

	lock_votable(votable);
	enable = is_override_vote_enabled_locked(votable);
	unlock_votable(votable);

	return enable;
}
EXPORT_SYMBOL_GPL(is_override_vote_enabled);

/**
 * is_client_vote_enabled() -
 * is_client_vote_enabled_locked() -
 *		The unlocked and locked variants of getting whether a client's
		vote is enabled.
 * @votable:	the votable object
 * @client_str: client of interest
 *
 * Returns:
 *	True if the client's vote is enabled; false otherwise.
 */
bool is_client_vote_enabled_locked(struct votable *votable,
							const char *client_str)
{

	int client_id;

	if (!votable || !client_str)
		return false;

	client_id = get_client_id(votable, client_str);
	if (client_id < 0)
		return false;

	return votable->votes[client_id].enabled;
}
EXPORT_SYMBOL_GPL(is_client_vote_enabled_locked);

bool is_client_vote_enabled(struct votable *votable, const char *client_str)
{
	bool enabled;

	if (!votable || !client_str)
		return false;

	lock_votable(votable);
	enabled = is_client_vote_enabled_locked(votable, client_str);
	unlock_votable(votable);
	return enabled;
}
EXPORT_SYMBOL_GPL(is_client_vote_enabled);

/**
 * get_client_vote() -
 * get_client_vote_locked() -
 *		The unlocked and locked variants of getting a client's voted
 *		value.
 * @votable:	the votable object
 * @client_str: client of interest
 *
 * Returns:
 *	The value the client voted for. -EINVAL is returned if the client
 *	is not enabled or the client is not found.
 */
int get_client_vote_locked(struct votable *votable, const char *client_str)
{
	int client_id;

	if (!votable || !client_str)
		return -EINVAL;

	client_id = get_client_id(votable, client_str);
	if (client_id < 0)
		return -EINVAL;

	if ((votable->type != VOTE_SET_ANY)
		&& !votable->votes[client_id].enabled)
		return -EINVAL;

	return votable->votes[client_id].value;
}
EXPORT_SYMBOL_GPL(get_client_vote_locked);

int get_client_vote(struct votable *votable, const char *client_str)
{
	int value;

	if (!votable || !client_str)
		return -EINVAL;

	lock_votable(votable);
	value = get_client_vote_locked(votable, client_str);
	unlock_votable(votable);
	return value;
}
EXPORT_SYMBOL_GPL(get_client_vote);

/**
 * get_effective_result() -
 * get_effective_result_locked() -
 *		The unlocked and locked variants of getting the effective value
 *		amongst all the enabled voters.
 *
 * @votable:	the votable object
 *
 * Returns:
 *	The effective result.
 *	For MIN and MAX votable, returns -EINVAL when the votable
 *	object has been created but no clients have casted their votes or
 *	the last enabled client disables its vote.
 *	For SET_ANY votable it returns 0 when no clients have casted their votes
 *	because for SET_ANY there is no concept of abstaining from election. The
 *	votes for all the clients of SET_ANY votable is defaulted to false.
 */
int get_effective_result_locked(struct votable *votable)
{
	if (!votable)
		return -EINVAL;

	if (votable->force_active)
		return votable->force_val;

	if (votable->override_result != -EINVAL)
		return votable->override_result;

	return votable->effective_result;
}
EXPORT_SYMBOL_GPL(get_effective_result_locked);

int get_effective_result(struct votable *votable)
{
	int value;

	if (!votable)
		return -EINVAL;

	lock_votable(votable);
	value = get_effective_result_locked(votable);
	unlock_votable(votable);
	return value;
}
EXPORT_SYMBOL_GPL(get_effective_result);

/**
 * get_effective_client() -
 * get_effective_client_locked() -
 *		The unlocked and locked variants of getting the effective client
 *		amongst all the enabled voters.
 *
 * @votable:	the votable object
 *
 * Returns:
 *	The effective client.
 *	For MIN and MAX votable, returns NULL when the votable
 *	object has been created but no clients have casted their votes or
 *	the last enabled client disables its vote.
 *	For SET_ANY votable it returns NULL too when no clients have casted
 *	their votes. But for SET_ANY since there is no concept of abstaining
 *	from election, the only client that casts a vote or the client that
 *	caused the result to change is returned.
 */
const char *get_effective_client_locked(struct votable *votable)
{
	if (!votable)
		return NULL;

	if (votable->force_active)
		return DEBUG_FORCE_CLIENT;

	if (votable->override_result != -EINVAL)
		return votable->override_client;

	return get_client_str(votable, votable->effective_client_id);
}
EXPORT_SYMBOL_GPL(get_effective_client_locked);

const char *get_effective_client(struct votable *votable)
{
	const char *client_str;

	if (!votable)
		return NULL;

	lock_votable(votable);
	client_str = get_effective_client_locked(votable);
	unlock_votable(votable);
	return client_str;
}
EXPORT_SYMBOL_GPL(get_effective_client);

/**
 * vote() -
 *
 * @votable:	the votable object
 * @client_str: the voting client
 * @enabled:	This provides a means for the client to exclude himself from
 *		election. This clients val (the next argument) will be
 *		considered only when he has enabled his participation.
 *		Note that this takes a differnt meaning for SET_ANY type, as
 *		there is no concept of abstaining from participation.
 *		Enabled is treated as the boolean value the client is voting.
 * @val:	The vote value. This is ignored for SET_ANY votable types.
 *		For MIN, MAX votable types this value is used as the
 *		clients vote value when the enabled is true, this value is
 *		ignored if enabled is false.
 *
 * The callback is called only when there is a change in the election results or
 * if it is the first time someone is voting.
 *
 * Returns:
 *	The return from the callback when present and needs to be called
 *	or zero.
 */
int vote(struct votable *votable, const char *client_str, bool enabled, int val)
{
	int effective_id = -EINVAL;
	int effective_result;
	int client_id;
	int rc = 0;
	bool similar_vote = false;

	if (!votable || !client_str)
		return -EINVAL;

	lock_votable(votable);

	client_id = get_client_id(votable, client_str);
	if (client_id < 0) {
		rc = client_id;
		goto out;
	}

	/*
	 * for SET_ANY the val is to be ignored, set it
	 * to enabled so that the election still works based on
	 * value regardless of the type
	 */
	if (votable->type == VOTE_SET_ANY)
		val = enabled;

	if ((votable->votes[client_id].enabled == enabled) &&
		(votable->votes[client_id].value == val)) {
		pr_debug("%s: %s,%d same voting %s of val=%d\n",
				votable->name,
				client_str, client_id,
				enabled ? "on" : "off",
				val);
		similar_vote = true;
	}

	votable->votes[client_id].enabled = enabled;
	votable->votes[client_id].value = val;

	if (similar_vote && votable->voted_on) {
		pr_debug("%s: %s,%d Ignoring similar voting %s of val=%d\n",
			votable->name,
			client_str, client_id, enabled ? "on" : "off", val);
		goto out;
	}

	pr_debug("%s: %s,%d voting %s of val=%d\n",
		votable->name,
		client_str, client_id, enabled ? "on" : "off", val);
	switch (votable->type) {
	case VOTE_MIN:
		vote_min(votable, client_id, &effective_result, &effective_id);
		break;
	case VOTE_MAX:
		vote_max(votable, client_id, &effective_result, &effective_id);
		break;
	case VOTE_SET_ANY:
		vote_set_any(votable, client_id,
				&effective_result, &effective_id);
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Note that the callback is called with a NULL string and -EINVAL
	 * result when there are no enabled votes
	 */
	if (!votable->voted_on
			|| (effective_result != votable->effective_result)) {
		votable->effective_client_id = effective_id;
		votable->effective_result = effective_result;
		pr_debug("%s: effective voting is now %d voted by %s,%d\n",
			votable->name, effective_result,
			get_client_str(votable, effective_id),
			effective_id);
		if (votable->callback && !votable->force_active
				&& (votable->override_result == -EINVAL))
			rc = votable->callback(votable, votable->data,
					effective_result,
					get_client_str(votable, effective_id));
	}

	votable->voted_on = true;
out:
	unlock_votable(votable);
	return rc;
}
EXPORT_SYMBOL_GPL(vote);

/**
 * vote_override() -
 *
 * @votable:		The votable object
 * @override_client:	The voting client that will override other client's
 *			votes, that are already present. When force_active
 *			and override votes are set on a votable, force_active's
 *			client will have the higher priority and it's vote will
 *			be the effective one.
 * @enabled:		This provides a means for the override client to exclude
 *			itself from election. This client's vote
 *			(the next argument) will be considered only when
 *			it has enabled its participation. When this is
 *			set true, this will force a value on a MIN/MAX votable
 *			irrespective of its current value.
 * @val:		The vote value. This will be effective only if enabled
 *			is set true.
 * Returns:
 *	The result of vote. 0 is returned if the vote
 *	is successfully set by the overriding client, when enabled is set.
 */
int vote_override(struct votable *votable, const char *override_client,
		  bool enabled, int val)
{
	int rc = 0;

	if (!votable || !override_client)
		return -EINVAL;

	lock_votable(votable);
	if (votable->force_active) {
		votable->override_result = enabled ? val : -EINVAL;
		goto out;
	}

	if (enabled) {
		rc = votable->callback(votable, votable->data,
					val, override_client);
		if (!rc) {
			votable->override_client = override_client;
			votable->override_result = val;
		}
	} else {
		rc = votable->callback(votable, votable->data,
			votable->effective_result,
			get_client_str(votable, votable->effective_client_id));
		votable->override_result = -EINVAL;
	}

out:
	unlock_votable(votable);
	return rc;
}
EXPORT_SYMBOL_GPL(vote_override);

int rerun_election(struct votable *votable)
{
	int rc = 0;
	int effective_result;

	if (!votable)
		return -EINVAL;

	lock_votable(votable);
	effective_result = get_effective_result_locked(votable);
	if (votable->callback)
		rc = votable->callback(votable,
			votable->data,
			effective_result,
			get_client_str(votable, votable->effective_client_id));
	unlock_votable(votable);
	return rc;
}
EXPORT_SYMBOL_GPL(rerun_election);

struct votable *find_votable(const char *name)
{
	unsigned long flags;
	struct votable *v;
	bool found = false;

	if (!name)
		return NULL;

	spin_lock_irqsave(&votable_list_slock, flags);
	if (list_empty(&votable_list))
		goto out;

	list_for_each_entry(v, &votable_list, list) {
		if (strcmp(v->name, name) == 0) {
			found = true;
			break;
		}
	}
out:
	spin_unlock_irqrestore(&votable_list_slock, flags);

	if (found)
		return v;
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(find_votable);

static int force_active_get(void *data, u64 *val)
{
	struct votable *votable = data;

	*val = votable->force_active;

	return 0;
}

static int force_active_set(void *data, u64 val)
{
	struct votable *votable = data;
	int rc = 0;
	int effective_result;
	const char *client;

	lock_votable(votable);
	votable->force_active = !!val;

	if (!votable->callback)
		goto out;

	if (votable->force_active) {
		rc = votable->callback(votable, votable->data,
			votable->force_val,
			DEBUG_FORCE_CLIENT);
	} else {
		if (votable->override_result != -EINVAL) {
			effective_result = votable->override_result;
			client = votable->override_client;
		} else {
			effective_result = votable->effective_result;
			client = get_client_str(votable,
					votable->effective_client_id);
		}
		rc = votable->callback(votable, votable->data, effective_result,
					client);
	}
out:
	unlock_votable(votable);
	return rc;
}
DEFINE_DEBUGFS_ATTRIBUTE(votable_force_ops, force_active_get, force_active_set,
		"%lld\n");

static int show_votable_clients(struct seq_file *m, void *data)
{
	struct votable *votable = m->private;
	int i;
	char *type_str = "Unkonwn";
	const char *effective_client_str;

	lock_votable(votable);

	for (i = 0; i < votable->num_clients; i++) {
		if (votable->client_strs[i]) {
			seq_printf(m, "%s: %s:\t\t\ten=%d v=%d\n",
					votable->name,
					votable->client_strs[i],
					votable->votes[i].enabled,
					votable->votes[i].value);
		}
	}

	switch (votable->type) {
	case VOTE_MIN:
		type_str = "Min";
		break;
	case VOTE_MAX:
		type_str = "Max";
		break;
	case VOTE_SET_ANY:
		type_str = "Set_any";
		break;
	}

	effective_client_str = get_effective_client_locked(votable);
	seq_printf(m, "%s: effective=%s type=%s v=%d\n",
			votable->name,
			effective_client_str ? effective_client_str : "none",
			type_str,
			get_effective_result_locked(votable));
	unlock_votable(votable);

	return 0;
}

static int votable_status_open(struct inode *inode, struct file *file)
{
	struct votable *votable = inode->i_private;

	return single_open(file, show_votable_clients, votable);
}

static const struct file_operations votable_status_ops = {
	.owner		= THIS_MODULE,
	.open		= votable_status_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

struct votable *create_votable(const char *name,
				int votable_type,
				int (*callback)(struct votable *votable,
					void *data,
					int effective_result,
					const char *effective_client),
				void *data)
{
	struct votable *votable;
	unsigned long flags;

	if (!name)
		return ERR_PTR(-EINVAL);

	votable = find_votable(name);
	if (votable)
		return ERR_PTR(-EEXIST);

	if (debug_root == NULL) {
		debug_root = debugfs_create_dir("pmic-votable", NULL);
		if (!debug_root) {
			pr_err("Couldn't create debug dir\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	if (votable_type >= NUM_VOTABLE_TYPES) {
		pr_err("Invalid votable_type specified for voter\n");
		return ERR_PTR(-EINVAL);
	}

	votable = kzalloc(sizeof(struct votable), GFP_KERNEL);
	if (!votable)
		return ERR_PTR(-ENOMEM);

	votable->name = kstrdup(name, GFP_KERNEL);
	if (!votable->name) {
		kfree(votable);
		return ERR_PTR(-ENOMEM);
	}

	votable->num_clients = NUM_MAX_CLIENTS;
	votable->callback = callback;
	votable->type = votable_type;
	votable->data = data;
	votable->override_result = -EINVAL;
	mutex_init(&votable->vote_lock);

	/*
	 * Because effective_result and client states are invalid
	 * before the first vote, initialize them to -EINVAL
	 */
	votable->effective_result = -EINVAL;
	if (votable->type == VOTE_SET_ANY)
		votable->effective_result = 0;
	votable->effective_client_id = -EINVAL;

	spin_lock_irqsave(&votable_list_slock, flags);
	list_add(&votable->list, &votable_list);
	spin_unlock_irqrestore(&votable_list_slock, flags);

	votable->root = debugfs_create_dir(name, debug_root);
	if (!votable->root) {
		pr_err("Couldn't create debug dir %s\n", name);
		kfree(votable->name);
		kfree(votable);
		return ERR_PTR(-ENOMEM);
	}

	votable->status_ent = debugfs_create_file("status", S_IFREG | 0444,
				  votable->root, votable,
				  &votable_status_ops);
	if (!votable->status_ent) {
		pr_err("Couldn't create status dbg file for %s\n", name);
		debugfs_remove_recursive(votable->root);
		kfree(votable->name);
		kfree(votable);
		return ERR_PTR(-EEXIST);
	}

	debugfs_create_u32("force_val",
			S_IFREG | 0644,
			votable->root,
			&(votable->force_val));

	votable->force_active_ent = debugfs_create_file("force_active",
					S_IFREG | 0444,
					votable->root, votable,
					&votable_force_ops);
	if (!votable->force_active_ent) {
		pr_err("Couldn't create force_active dbg file for %s\n", name);
		debugfs_remove_recursive(votable->root);
		kfree(votable->name);
		kfree(votable);
		return ERR_PTR(-EEXIST);
	}

	return votable;
}
EXPORT_SYMBOL_GPL(create_votable);

void destroy_votable(struct votable *votable)
{
	unsigned long flags;
	int i;

	if (!votable)
		return;

	spin_lock_irqsave(&votable_list_slock, flags);
	list_del(&votable->list);
	spin_unlock_irqrestore(&votable_list_slock, flags);

	debugfs_remove_recursive(votable->root);

	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++)
		kfree(votable->client_strs[i]);

	kfree(votable->name);
	kfree(votable);
}
EXPORT_SYMBOL_GPL(destroy_votable);
