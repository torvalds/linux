/*
 * Wifi power save module.
 *
 * Yongle Lai @ Rockchip Fuzhou
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/timer.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
#include <linux/android_power.h>
#endif
#include <linux/netdevice.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/inet_hashtables.h>

#include "wifi_power.h"

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
extern struct inet_hashinfo __cacheline_aligned tcp_hashinfo;
#else
extern struct inet_hashinfo tcp_hashinfo;
#endif

struct timer_list	wifi_ps_timer;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
android_suspend_lock_t      wifi_android_lock;
#endif

#define TIMER_INTERVAL	2000 //unit is micro second.
#define IDLE_SW_COUNT   8	 //16 seconds from BUSY to IDLE		
/*
 * When wifi driver isn't ready. We should be IDLE.
 */
volatile static int wifi_net_state = WIFI_NETWORK_IDLE;
struct net_device *wlan_netdev = NULL;
unsigned long wifi_iptx_packets = 0, wifi_iptx_packets_back = 0;
unsigned long wifi_iprx_packets = 0, wifi_iprx_packets_back = 0;
unsigned int wifi_iptx = 0, wifi_iprx = 0;

int (*wifi_power_save_callback)(int status) = NULL;

int wifi_ps_active = 0; //Whether we should work.

void wifi_power_save_suspend(void)
{
	wifi_ps_active = 0;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	//android_lock_suspend(&wifi_android_lock);
#endif
}

void wifi_power_save_resume(void)
{
	wifi_ps_active = 1;
}

int wifi_power_save_register_callback(int (*callback)(int status))
{
	//printk("%s: enter....\n", __func__);
	
	wifi_power_save_callback = callback;

	return 0;
}

/*
 * Do statistics 20 seconds.
 */
int wifi_evaluate_rx_traffic(void)
{
	//int ret = 1;
	static int count = 0;
	
	if (wlan_netdev == NULL)
	{
		printk("wlan_netdev is NULL.\n");
		return 1;
	}

	if (count < 10) /* 10 * 2 = 20 seconds */
	{
		count++;
		return wifi_iprx;
	}

	count = 0;
	
	wifi_iprx_packets = wlan_netdev->stats.rx_packets;

	/*
	 * 3 packets in 20 seconds mean connection is active.
	 */
	if ((wifi_iprx_packets - wifi_iprx_packets_back) >= 2)
	{
		wifi_iprx = 1;
	}
	else
	{
		wifi_iprx = 0;
	}

	wifi_iprx_packets_back = wifi_iprx_packets;
	
	return wifi_iprx;
}

int wifi_evaluate_tx_traffic(void)
{
	//int ret = 1;
	static int count = 0;
	
	if (wlan_netdev == NULL)
	{
		printk("wlan_netdev is NULL.\n");
		return 1;
	}

	if (count < 10) /* 10 * 2 = 20 seconds */
	{
		count++;
		return wifi_iptx;
	}

	count = 0;
	
	wifi_iptx_packets = wlan_netdev->stats.tx_packets;

	/*
	 * 2 packets in 20 seconds mean connection is active.
	 */
	if ((wifi_iptx_packets - wifi_iptx_packets_back) >= 2)
	{
		wifi_iptx = 1;
	}
	else
	{
		wifi_iptx = 0;
	}

	wifi_iptx_packets_back = wifi_iptx_packets;
	
	return wifi_iptx;
}

#if 0
int wifi_evaluate_traffic(void)
{
	int ret = 1;
	
	if (wlan_netdev == NULL)
	{
		printk("wlan_netdev is NULL.\n");
		return 1;
	}

	wifi_iprx_packets = wlan_netdev->stats.rx_packets;
	wifi_iptx_packets = wlan_netdev->stats.tx_packets;

	/*
	 * 3 packets in 10 seconds mean connection is active.
	 */
	if ((wifi_iptx_packets - wifi_iptx_packets_back) >= 2)
		wifi_iptx = 0;
	else
		wifi_iptx++;

	if ((wifi_iprx_packets - wifi_iprx_packets_back) >= 2)
		wifi_iprx = 0;
	else
		wifi_iprx++;
	
	//printk("wifi_packets: TX = %lu    RX = %lu ", wifi_iptx_packets, wifi_iprx_packets);
	//printk("NO IP: TX = %u    RX = %u\n", wifi_iptx, wifi_iprx);

	/* NO tx and rx, we are idle. */
	if ((wifi_iptx >= 10) && (wifi_iprx >= 10))
		ret = 0;
	
	wifi_iptx_packets_back = wifi_iptx_packets;
	wifi_iprx_packets_back = wifi_iprx_packets;
	
	return ret;
}
#endif

int wifi_check_tcp_connection(void)
{
	int bucket, num = 0;
	struct inet_sock *inet = NULL;
	__be32 dest;
	
	for (bucket = 0; bucket < tcp_hashinfo.ehash_size; ++bucket) 
	{
		struct sock *sk;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
		struct hlist_node *node;
		rwlock_t *lock = inet_ehash_lockp(&tcp_hashinfo, bucket);

		read_lock_bh(lock);

		sk_for_each(sk, node, &tcp_hashinfo.ehash[bucket].chain)
#else
		struct hlist_nulls_node *node;
		spinlock_t *lock = inet_ehash_lockp(&tcp_hashinfo, bucket);

		spin_lock_bh(lock);

		sk_nulls_for_each(sk, node, &tcp_hashinfo.ehash[bucket].chain)
#endif
		{
			if ((sk->__sk_common.skc_state != TCP_ESTABLISHED) &&
				(sk->__sk_common.skc_state != TCP_SYN_SENT) &&
				(sk->__sk_common.skc_state != TCP_SYN_SENT))
				continue;

			/*
			 * tcp 0 0 127.0.0.1:34937 127.0.0.1:54869 ESTABLISHED
			 */
			inet = inet_sk(sk);
			dest = inet->daddr;
			//printk("destination address: %x\n", dest);
			if ((dest == 0x100007F) || (dest == 0))
				continue;
			
			num++;
			//printk("connection state: %x\n", sk->__sk_common.skc_state);
		}
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
		read_unlock_bh(lock);
#else
		spin_unlock_bh(lock);
#endif
	}

	return num;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,25)
extern struct udp_table udp_table;
#endif

int wifi_check_udp_connection(void)
{
	struct sock *sk;
	int num = 0, bucket;
	struct inet_sock *inet = NULL;
	__be32 dest;
	
	

	for (bucket = 0; bucket < UDP_HTABLE_SIZE; ++bucket) 
	{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
		struct hlist_node *node;

		read_lock(&udp_hash_lock);
		
		sk_for_each(sk, node, udp_hash + bucket) 
#else
		struct hlist_nulls_node *node;
		struct udp_hslot *hslot = &udp_table.hash[bucket];

		spin_lock_bh(&hslot->lock);

		sk_nulls_for_each(sk, node, &hslot->head) 
#endif
		{
			inet = inet_sk(sk);
			dest = inet->daddr;

			if ((dest == 0x100007F) || (dest == 0))
				continue;
			
			num++;
		}
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
		read_unlock(&udp_hash_lock);
#else
		spin_unlock_bh(&hslot->lock);
#endif
	}

	return num;
}

int wifi_check_mcast_connection(void)
{
	int ret = 0;

	//printk("Multicast group: %d\n", wlan_netdev->mc_count);

	if (wlan_netdev && (wlan_netdev->mc_count > 1))
		ret = wlan_netdev->mc_count - 1;
	
	return ret;
}

int idle_count = 0;

static int wifi_ps_sm(int new_state)
{
	if (new_state == WIFI_NETWORK_BUSY)
	{
		idle_count = 0;
		return WIFI_NETWORK_BUSY;
	}
	
	if (new_state == WIFI_NETWORK_IDLE)
	{
		if (idle_count >= IDLE_SW_COUNT)
			return WIFI_NETWORK_IDLE;
		else
		{
			idle_count++;
			return WIFI_NETWORK_BUSY;
		}
	}

	return WIFI_NETWORK_IDLE;
}

static void wifi_ps_timer_fn(unsigned long data)
{
	int tcp, udp, tx = 0, rx = 0, new_state;
	int mcast = 0;

	if (wifi_ps_active == 0)
	{
		printk("Power save module is IDLE for now.\n");
		goto out;
	}
	
	tcp = wifi_check_tcp_connection();
	udp = wifi_check_udp_connection();
	tx = wifi_evaluate_tx_traffic();
	rx = wifi_evaluate_rx_traffic();
	mcast = wifi_check_mcast_connection();
	//printk("Network state: m=%d t=%d u=%d tx=%d rx=%d.\n", mcast, tcp, udp, tx, rx);
	
	/*
	 * Initially, multicast group number is 1.
	 */
	if ((tcp == 0) && (udp == 0) && (mcast <= 0))
	{
		new_state = wifi_ps_sm(WIFI_NETWORK_IDLE);
	}
	else if ((wifi_net_state == WIFI_NETWORK_IDLE) &&
		     ((tcp > 0) || (udp > 0)))
	{
		new_state = wifi_ps_sm(WIFI_NETWORK_BUSY);
	}
	else if ((tx == 0) && (rx == 0))
	{
		new_state = wifi_ps_sm(WIFI_NETWORK_IDLE);
	}
	else
	{
		new_state = wifi_ps_sm(WIFI_NETWORK_BUSY);
	}
	
	if (new_state != wifi_net_state)
	{
		wifi_net_state = new_state;
		if (new_state == WIFI_NETWORK_IDLE)
		{
			printk("Network is IDLE, m=%d t=%d u=%d tx=%d rx=%d.\n", mcast, tcp, udp, tx, rx);
			
			if (wifi_power_save_callback != NULL)
				wifi_power_save_callback(WIFI_NETWORK_IDLE);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)			
			android_unlock_suspend(&wifi_android_lock);
#endif
		}
		else
		{
			printk("Network is BUSY, m=%d t=%d u=%d tx=%d rx=%d.\n", mcast, tcp, udp, tx, rx);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)			
			android_lock_suspend(&wifi_android_lock);
#endif
			if (wifi_power_save_callback != NULL)
				wifi_power_save_callback(WIFI_NETWORK_BUSY);
		}
	}
out:
	mod_timer(&wifi_ps_timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));
}

/*
 * Return the current network status, which indicates
 * whether network is busy or not.
 */
int wifi_power_save_state(void)
{
	return wifi_net_state;
}

int wifi_power_save_init(void)
{
	int ret = 0;

	//wifi_power_save_callback = NULL;

	wifi_ps_active = 1;
	wifi_iptx_packets = 0;
	wifi_iprx_packets = 0;
	wifi_iptx_packets_back = 0;
	wifi_iprx_packets_back = 0;
	wifi_iptx = 1;
	wifi_iprx = 1;
	idle_count = 0;
	
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	wifi_android_lock.name = "wifidev";

    android_init_suspend_lock(&wifi_android_lock);
    android_lock_suspend(&wifi_android_lock);
#endif
	wifi_net_state = WIFI_NETWORK_BUSY;

	wlan_netdev = dev_get_by_name(&init_net, "wlan0");
	if (wlan_netdev == NULL)
		printk("%s: couldn't find net_device for wlan0.\n", __func__);
	
	/*
	 * Create a timer to check network status.
	 */
	setup_timer(&wifi_ps_timer, wifi_ps_timer_fn, (unsigned long)0);
	mod_timer(&wifi_ps_timer, jiffies + msecs_to_jiffies(15000));
	
	return ret;
}

int wifi_power_save_stop(void)
{
	wifi_ps_active = 0;
	msleep(3);
	
	del_timer_sync(&wifi_ps_timer);

	if (wlan_netdev != NULL)
		dev_put(wlan_netdev);

	wlan_netdev = NULL;
	
	return 0;
}

int wifi_power_save_exit(void)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	android_unlock_suspend(&wifi_android_lock);
	android_uninit_suspend_lock(&wifi_android_lock);
#endif		
	wifi_net_state = WIFI_NETWORK_IDLE;

	wifi_power_save_callback = NULL;

	wifi_iptx_packets = 0;
	wifi_iprx_packets = 0;
	wifi_iptx_packets_back = 0;
	wifi_iprx_packets_back = 0;
	wifi_iptx = 0;
	wifi_iprx = 0;
		
	return 0;
}

