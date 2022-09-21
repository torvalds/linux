// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifdef DEBUG

#include <linux/jiffies.h>
#include <linux/hrtimer.h>

static const struct {
	bool result;
	u64 nsec_to_sleep_before;
} expected_results[] __initconst = {
	[0 ... PACKETS_BURSTABLE - 1] = { true, 0 },
	[PACKETS_BURSTABLE] = { false, 0 },
	[PACKETS_BURSTABLE + 1] = { true, NSEC_PER_SEC / PACKETS_PER_SECOND },
	[PACKETS_BURSTABLE + 2] = { false, 0 },
	[PACKETS_BURSTABLE + 3] = { true, (NSEC_PER_SEC / PACKETS_PER_SECOND) * 2 },
	[PACKETS_BURSTABLE + 4] = { true, 0 },
	[PACKETS_BURSTABLE + 5] = { false, 0 }
};

static __init unsigned int maximum_jiffies_at_index(int index)
{
	u64 total_nsecs = 2 * NSEC_PER_SEC / PACKETS_PER_SECOND / 3;
	int i;

	for (i = 0; i <= index; ++i)
		total_nsecs += expected_results[i].nsec_to_sleep_before;
	return nsecs_to_jiffies(total_nsecs);
}

static __init int timings_test(struct sk_buff *skb4, struct iphdr *hdr4,
			       struct sk_buff *skb6, struct ipv6hdr *hdr6,
			       int *test)
{
	unsigned long loop_start_time;
	int i;

	wg_ratelimiter_gc_entries(NULL);
	rcu_barrier();
	loop_start_time = jiffies;

	for (i = 0; i < ARRAY_SIZE(expected_results); ++i) {
		if (expected_results[i].nsec_to_sleep_before) {
			ktime_t timeout = ktime_add(ktime_add_ns(ktime_get_coarse_boottime(), TICK_NSEC * 4 / 3),
						    ns_to_ktime(expected_results[i].nsec_to_sleep_before));
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_hrtimeout_range_clock(&timeout, 0, HRTIMER_MODE_ABS, CLOCK_BOOTTIME);
		}

		if (time_is_before_jiffies(loop_start_time +
					   maximum_jiffies_at_index(i)))
			return -ETIMEDOUT;
		if (wg_ratelimiter_allow(skb4, &init_net) !=
					expected_results[i].result)
			return -EXFULL;
		++(*test);

		hdr4->saddr = htonl(ntohl(hdr4->saddr) + i + 1);
		if (time_is_before_jiffies(loop_start_time +
					   maximum_jiffies_at_index(i)))
			return -ETIMEDOUT;
		if (!wg_ratelimiter_allow(skb4, &init_net))
			return -EXFULL;
		++(*test);

		hdr4->saddr = htonl(ntohl(hdr4->saddr) - i - 1);

#if IS_ENABLED(CONFIG_IPV6)
		hdr6->saddr.in6_u.u6_addr32[2] = htonl(i);
		hdr6->saddr.in6_u.u6_addr32[3] = htonl(i);
		if (time_is_before_jiffies(loop_start_time +
					   maximum_jiffies_at_index(i)))
			return -ETIMEDOUT;
		if (wg_ratelimiter_allow(skb6, &init_net) !=
					expected_results[i].result)
			return -EXFULL;
		++(*test);

		hdr6->saddr.in6_u.u6_addr32[0] =
			htonl(ntohl(hdr6->saddr.in6_u.u6_addr32[0]) + i + 1);
		if (time_is_before_jiffies(loop_start_time +
					   maximum_jiffies_at_index(i)))
			return -ETIMEDOUT;
		if (!wg_ratelimiter_allow(skb6, &init_net))
			return -EXFULL;
		++(*test);

		hdr6->saddr.in6_u.u6_addr32[0] =
			htonl(ntohl(hdr6->saddr.in6_u.u6_addr32[0]) - i - 1);

		if (time_is_before_jiffies(loop_start_time +
					   maximum_jiffies_at_index(i)))
			return -ETIMEDOUT;
#endif
	}
	return 0;
}

static __init int capacity_test(struct sk_buff *skb4, struct iphdr *hdr4,
				int *test)
{
	int i;

	wg_ratelimiter_gc_entries(NULL);
	rcu_barrier();

	if (atomic_read(&total_entries))
		return -EXFULL;
	++(*test);

	for (i = 0; i <= max_entries; ++i) {
		hdr4->saddr = htonl(i);
		if (wg_ratelimiter_allow(skb4, &init_net) != (i != max_entries))
			return -EXFULL;
		++(*test);
	}
	return 0;
}

bool __init wg_ratelimiter_selftest(void)
{
	enum { TRIALS_BEFORE_GIVING_UP = 5000 };
	bool success = false;
	int test = 0, trials;
	struct sk_buff *skb4, *skb6 = NULL;
	struct iphdr *hdr4;
	struct ipv6hdr *hdr6 = NULL;

	if (IS_ENABLED(CONFIG_KASAN) || IS_ENABLED(CONFIG_UBSAN))
		return true;

	BUILD_BUG_ON(NSEC_PER_SEC % PACKETS_PER_SECOND != 0);

	if (wg_ratelimiter_init())
		goto out;
	++test;
	if (wg_ratelimiter_init()) {
		wg_ratelimiter_uninit();
		goto out;
	}
	++test;
	if (wg_ratelimiter_init()) {
		wg_ratelimiter_uninit();
		wg_ratelimiter_uninit();
		goto out;
	}
	++test;

	skb4 = alloc_skb(sizeof(struct iphdr), GFP_KERNEL);
	if (unlikely(!skb4))
		goto err_nofree;
	skb4->protocol = htons(ETH_P_IP);
	hdr4 = (struct iphdr *)skb_put(skb4, sizeof(*hdr4));
	hdr4->saddr = htonl(8182);
	skb_reset_network_header(skb4);
	++test;

#if IS_ENABLED(CONFIG_IPV6)
	skb6 = alloc_skb(sizeof(struct ipv6hdr), GFP_KERNEL);
	if (unlikely(!skb6)) {
		kfree_skb(skb4);
		goto err_nofree;
	}
	skb6->protocol = htons(ETH_P_IPV6);
	hdr6 = (struct ipv6hdr *)skb_put(skb6, sizeof(*hdr6));
	hdr6->saddr.in6_u.u6_addr32[0] = htonl(1212);
	hdr6->saddr.in6_u.u6_addr32[1] = htonl(289188);
	skb_reset_network_header(skb6);
	++test;
#endif

	for (trials = TRIALS_BEFORE_GIVING_UP;;) {
		int test_count = 0, ret;

		ret = timings_test(skb4, hdr4, skb6, hdr6, &test_count);
		if (ret == -ETIMEDOUT) {
			if (!trials--) {
				test += test_count;
				goto err;
			}
			continue;
		} else if (ret < 0) {
			test += test_count;
			goto err;
		} else {
			test += test_count;
			break;
		}
	}

	for (trials = TRIALS_BEFORE_GIVING_UP;;) {
		int test_count = 0;

		if (capacity_test(skb4, hdr4, &test_count) < 0) {
			if (!trials--) {
				test += test_count;
				goto err;
			}
			continue;
		}
		test += test_count;
		break;
	}

	success = true;

err:
	kfree_skb(skb4);
#if IS_ENABLED(CONFIG_IPV6)
	kfree_skb(skb6);
#endif
err_nofree:
	wg_ratelimiter_uninit();
	wg_ratelimiter_uninit();
	wg_ratelimiter_uninit();
	/* Uninit one extra time to check underflow detection. */
	wg_ratelimiter_uninit();
out:
	if (success)
		pr_info("ratelimiter self-tests: pass\n");
	else
		pr_err("ratelimiter self-test %d: FAIL\n", test);

	return success;
}
#endif
