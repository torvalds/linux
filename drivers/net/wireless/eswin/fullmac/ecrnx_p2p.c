/**
 ****************************************************************************************
 *
 * @file rwnx_p2p.c
 *
 * 
 *
 ****************************************************************************************
 */

/**
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "ecrnx_p2p.h"
#include "ecrnx_msg_tx.h"


/**
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
static void ecrnx_p2p_discovery_timer_fn(struct timer_list *t)
#else
static void ecrnx_p2p_discovery_timer_fn(ulong x)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
	struct ecrnx_hw *ecrnx_hw = from_timer(ecrnx_hw, t, p2p_listen.listen_timer);
#else
	struct ecrnx_hw *ecrnx_hw = (void *)x;
#endif
	ECRNX_PRINT("ecrnx_p2p_discovery_timer_fn\n");

	schedule_work(&ecrnx_hw->p2p_listen.listen_expired_work);

}

void ecrnx_p2p_listen_init(struct ecrnx_p2p_listen *p2p_listen)
{
	struct ecrnx_hw *ecrnx_hw = container_of(p2p_listen, struct ecrnx_hw, p2p_listen);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
	timer_setup(&p2p_listen->listen_timer, ecrnx_p2p_discovery_timer_fn, 0);
#else
	setup_timer(&p2p_listen->listen_timer, ecrnx_p2p_discovery_timer_fn, (ulong)ecrnx_hw);
#endif
	INIT_WORK(&p2p_listen->listen_expired_work, ecrnx_p2p_listen_expired);

    init_waitqueue_head(&p2p_listen->rxdataq);
}

void ecrnx_p2p_listen_expired(struct work_struct *work)
{
	struct ecrnx_p2p_listen *p2p_listen = container_of(work, struct ecrnx_p2p_listen, listen_expired_work);
	struct ecrnx_hw *ecrnx_hw = container_of(p2p_listen, struct ecrnx_hw, p2p_listen);

	ECRNX_PRINT("p2p_listen_expired\n");

	if (p2p_listen->listen_started)
	{
		del_timer_sync(&p2p_listen->listen_timer);
		ecrnx_send_p2p_cancel_listen_req(ecrnx_hw, p2p_listen->ecrnx_vif);
	}
	else
	{
		return;
	}
}

