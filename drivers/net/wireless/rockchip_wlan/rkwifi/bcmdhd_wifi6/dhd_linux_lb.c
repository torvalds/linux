/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom Dongle Host Driver (DHD), Linux-specific network interface
 * Basically selected code segments from usb-cdc.c and usb-rndis.c
 *
 * Copyright (C) 1999-2019, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_linux_lb.c 805819 2019-02-20 10:49:35Z $
 */

#include <dhd_linux_priv.h>

extern dhd_pub_t* g_dhd_pub;

#if defined(DHD_LB)

void
dhd_lb_set_default_cpus(dhd_info_t *dhd)
{
	/* Default CPU allocation for the jobs */
	atomic_set(&dhd->rx_napi_cpu, 1);
	atomic_set(&dhd->rx_compl_cpu, 2);
	atomic_set(&dhd->tx_compl_cpu, 2);
	atomic_set(&dhd->tx_cpu, 2);
	atomic_set(&dhd->net_tx_cpu, 0);
}

void
dhd_cpumasks_deinit(dhd_info_t *dhd)
{
	free_cpumask_var(dhd->cpumask_curr_avail);
	free_cpumask_var(dhd->cpumask_primary);
	free_cpumask_var(dhd->cpumask_primary_new);
	free_cpumask_var(dhd->cpumask_secondary);
	free_cpumask_var(dhd->cpumask_secondary_new);
}

int
dhd_cpumasks_init(dhd_info_t *dhd)
{
	int id;
	uint32 cpus, num_cpus = num_possible_cpus();
	int ret = 0;

	DHD_ERROR(("%s CPU masks primary(big)=0x%x secondary(little)=0x%x\n", __FUNCTION__,
		DHD_LB_PRIMARY_CPUS, DHD_LB_SECONDARY_CPUS));

	if (!alloc_cpumask_var(&dhd->cpumask_curr_avail, GFP_KERNEL) ||
	    !alloc_cpumask_var(&dhd->cpumask_primary, GFP_KERNEL) ||
	    !alloc_cpumask_var(&dhd->cpumask_primary_new, GFP_KERNEL) ||
	    !alloc_cpumask_var(&dhd->cpumask_secondary, GFP_KERNEL) ||
	    !alloc_cpumask_var(&dhd->cpumask_secondary_new, GFP_KERNEL)) {
		DHD_ERROR(("%s Failed to init cpumasks\n", __FUNCTION__));
		ret = -ENOMEM;
		goto fail;
	}

	cpumask_copy(dhd->cpumask_curr_avail, cpu_online_mask);
	cpumask_clear(dhd->cpumask_primary);
	cpumask_clear(dhd->cpumask_secondary);

	if (num_cpus > 32) {
		DHD_ERROR(("%s max cpus must be 32, %d too big\n", __FUNCTION__, num_cpus));
		ASSERT(0);
	}

	cpus = DHD_LB_PRIMARY_CPUS;
	for (id = 0; id < num_cpus; id++) {
		if (isset(&cpus, id))
			cpumask_set_cpu(id, dhd->cpumask_primary);
	}

	cpus = DHD_LB_SECONDARY_CPUS;
	for (id = 0; id < num_cpus; id++) {
		if (isset(&cpus, id))
			cpumask_set_cpu(id, dhd->cpumask_secondary);
	}

	return ret;
fail:
	dhd_cpumasks_deinit(dhd);
	return ret;
}

/*
 * The CPU Candidacy Algorithm
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * The available CPUs for selection are divided into two groups
 *  Primary Set - A CPU mask that carries the First Choice CPUs
 *  Secondary Set - A CPU mask that carries the Second Choice CPUs.
 *
 * There are two types of Job, that needs to be assigned to
 * the CPUs, from one of the above mentioned CPU group. The Jobs are
 * 1) Rx Packet Processing - napi_cpu
 * 2) Completion Processiong (Tx, RX) - compl_cpu
 *
 * To begin with both napi_cpu and compl_cpu are on CPU0. Whenever a CPU goes
 * on-line/off-line the CPU candidacy algorithm is triggerd. The candidacy
 * algo tries to pickup the first available non boot CPU (CPU0) for napi_cpu.
 * If there are more processors free, it assigns one to compl_cpu.
 * It also tries to ensure that both napi_cpu and compl_cpu are not on the same
 * CPU, as much as possible.
 *
 * By design, both Tx and Rx completion jobs are run on the same CPU core, as it
 * would allow Tx completion skb's to be released into a local free pool from
 * which the rx buffer posts could have been serviced. it is important to note
 * that a Tx packet may not have a large enough buffer for rx posting.
 */
void dhd_select_cpu_candidacy(dhd_info_t *dhd)
{
	uint32 primary_available_cpus; /* count of primary available cpus */
	uint32 secondary_available_cpus; /* count of secondary available cpus */
	uint32 napi_cpu = 0; /* cpu selected for napi rx processing */
	uint32 compl_cpu = 0; /* cpu selected for completion jobs */
	uint32 tx_cpu = 0; /* cpu selected for tx processing job */

	cpumask_clear(dhd->cpumask_primary_new);
	cpumask_clear(dhd->cpumask_secondary_new);

	/*
	 * Now select from the primary mask. Even if a Job is
	 * already running on a CPU in secondary group, we still move
	 * to primary CPU. So no conditional checks.
	 */
	cpumask_and(dhd->cpumask_primary_new, dhd->cpumask_primary,
		dhd->cpumask_curr_avail);

	cpumask_and(dhd->cpumask_secondary_new, dhd->cpumask_secondary,
		dhd->cpumask_curr_avail);

	primary_available_cpus = cpumask_weight(dhd->cpumask_primary_new);

	if (primary_available_cpus > 0) {
		napi_cpu = cpumask_first(dhd->cpumask_primary_new);

		/* If no further CPU is available,
		 * cpumask_next returns >= nr_cpu_ids
		 */
		tx_cpu = cpumask_next(napi_cpu, dhd->cpumask_primary_new);
		if (tx_cpu >= nr_cpu_ids)
			tx_cpu = 0;

		/* In case there are no more CPUs, do completions & Tx in same CPU */
		compl_cpu = cpumask_next(tx_cpu, dhd->cpumask_primary_new);
		if (compl_cpu >= nr_cpu_ids)
			compl_cpu = tx_cpu;
	}

	DHD_INFO(("%s After primary CPU check napi_cpu %d compl_cpu %d tx_cpu %d\n",
		__FUNCTION__, napi_cpu, compl_cpu, tx_cpu));

	/* -- Now check for the CPUs from the secondary mask -- */
	secondary_available_cpus = cpumask_weight(dhd->cpumask_secondary_new);

	DHD_INFO(("%s Available secondary cpus %d nr_cpu_ids %d\n",
		__FUNCTION__, secondary_available_cpus, nr_cpu_ids));

	if (secondary_available_cpus > 0) {
		/* At this point if napi_cpu is unassigned it means no CPU
		 * is online from Primary Group
		 */
		if (napi_cpu == 0) {
			napi_cpu = cpumask_first(dhd->cpumask_secondary_new);
			tx_cpu = cpumask_next(napi_cpu, dhd->cpumask_secondary_new);
			compl_cpu = cpumask_next(tx_cpu, dhd->cpumask_secondary_new);
		} else if (tx_cpu == 0) {
			tx_cpu = cpumask_first(dhd->cpumask_secondary_new);
			compl_cpu = cpumask_next(tx_cpu, dhd->cpumask_secondary_new);
		} else if (compl_cpu == 0) {
			compl_cpu = cpumask_first(dhd->cpumask_secondary_new);
		}

		/* If no CPU was available for tx processing, choose CPU 0 */
		if (tx_cpu >= nr_cpu_ids)
			tx_cpu = 0;

		/* If no CPU was available for completion, choose CPU 0 */
		if (compl_cpu >= nr_cpu_ids)
			compl_cpu = 0;
	}
	if ((primary_available_cpus == 0) &&
		(secondary_available_cpus == 0)) {
		/* No CPUs available from primary or secondary mask */
		napi_cpu = 1;
		compl_cpu = 0;
		tx_cpu = 2;
	}

	DHD_INFO(("%s After secondary CPU check napi_cpu %d compl_cpu %d tx_cpu %d\n",
		__FUNCTION__, napi_cpu, compl_cpu, tx_cpu));

	ASSERT(napi_cpu < nr_cpu_ids);
	ASSERT(compl_cpu < nr_cpu_ids);
	ASSERT(tx_cpu < nr_cpu_ids);

	atomic_set(&dhd->rx_napi_cpu, napi_cpu);
	atomic_set(&dhd->tx_compl_cpu, compl_cpu);
	atomic_set(&dhd->rx_compl_cpu, compl_cpu);
	atomic_set(&dhd->tx_cpu, tx_cpu);

	return;
}

/*
 * Function to handle CPU Hotplug notifications.
 * One of the task it does is to trigger the CPU Candidacy algorithm
 * for load balancing.
 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))

int dhd_cpu_startup_callback(unsigned int cpu)
{
	dhd_info_t *dhd = g_dhd_pub->info;

	DHD_INFO(("%s(): \r\n cpu:%d", __FUNCTION__, cpu));
	DHD_LB_STATS_INCR(dhd->cpu_online_cnt[cpu]);
	cpumask_set_cpu(cpu, dhd->cpumask_curr_avail);
	dhd_select_cpu_candidacy(dhd);

	return 0;
}

int dhd_cpu_teardown_callback(unsigned int cpu)
{
	dhd_info_t *dhd = g_dhd_pub->info;

	DHD_INFO(("%s(): \r\n cpu:%d", __FUNCTION__, cpu));
	DHD_LB_STATS_INCR(dhd->cpu_offline_cnt[cpu]);
	cpumask_clear_cpu(cpu, dhd->cpumask_curr_avail);
	dhd_select_cpu_candidacy(dhd);

	return 0;
}
#else
int
dhd_cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	unsigned long int cpu = (unsigned long int)hcpu;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	dhd_info_t *dhd = container_of(nfb, dhd_info_t, cpu_notifier);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif

	if (!dhd || !(dhd->dhd_state & DHD_ATTACH_STATE_LB_ATTACH_DONE)) {
		DHD_INFO(("%s(): LB data is not initialized yet.\n",
			__FUNCTION__));
		return NOTIFY_BAD;
	}

	switch (action)
	{
		case CPU_ONLINE:
		case CPU_ONLINE_FROZEN:
			DHD_LB_STATS_INCR(dhd->cpu_online_cnt[cpu]);
			cpumask_set_cpu(cpu, dhd->cpumask_curr_avail);
			dhd_select_cpu_candidacy(dhd);
			break;

		case CPU_DOWN_PREPARE:
		case CPU_DOWN_PREPARE_FROZEN:
			DHD_LB_STATS_INCR(dhd->cpu_offline_cnt[cpu]);
			cpumask_clear_cpu(cpu, dhd->cpumask_curr_avail);
			dhd_select_cpu_candidacy(dhd);
			break;
		default:
			break;
	}

	return NOTIFY_OK;
}
#endif /* LINUX_VERSION_CODE < 4.10.0 */

int dhd_register_cpuhp_callback(dhd_info_t *dhd)
{
	int cpuhp_ret = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
	cpuhp_ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "dhd",
		dhd_cpu_startup_callback, dhd_cpu_teardown_callback);

	if (cpuhp_ret < 0) {
		DHD_ERROR(("%s(): cpuhp_setup_state failed %d RX LB won't happen \r\n",
			__FUNCTION__, cpuhp_ret));
	}
#else
	/*
	 * If we are able to initialize CPU masks, lets register to the
	 * CPU Hotplug framework to change the CPU for each job dynamically
	 * using candidacy algorithm.
	 */
	dhd->cpu_notifier.notifier_call = dhd_cpu_callback;
	register_hotcpu_notifier(&dhd->cpu_notifier); /* Register a callback */
#endif /* LINUX_VERSION_CODE < 4.10.0 */
	return cpuhp_ret;
}

int dhd_unregister_cpuhp_callback(dhd_info_t *dhd)
{
	int ret = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
	/* Don't want to call tear down while unregistering */
	cpuhp_remove_state_nocalls(CPUHP_AP_ONLINE_DYN);
#else
	if (dhd->cpu_notifier.notifier_call != NULL) {
		unregister_cpu_notifier(&dhd->cpu_notifier);
	}
#endif // endif
	return ret;
}

#if defined(DHD_LB_STATS)
void dhd_lb_stats_init(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	int i, j, num_cpus = num_possible_cpus();
	int alloc_size = sizeof(uint32) * num_cpus;

	if (dhdp == NULL) {
		DHD_ERROR(("%s(): Invalid argument dhd pubb pointer is NULL \n",
			__FUNCTION__));
		return;
	}

	dhd = dhdp->info;
	if (dhd == NULL) {
		DHD_ERROR(("%s(): DHD pointer is NULL \n", __FUNCTION__));
		return;
	}

	DHD_LB_STATS_CLR(dhd->dhd_dpc_cnt);
	DHD_LB_STATS_CLR(dhd->napi_sched_cnt);

	dhd->napi_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->napi_percpu_run_cnt) {
		DHD_ERROR(("%s(): napi_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->napi_percpu_run_cnt[i]);

	DHD_LB_STATS_CLR(dhd->rxc_sched_cnt);

	dhd->rxc_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->rxc_percpu_run_cnt) {
		DHD_ERROR(("%s(): rxc_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->rxc_percpu_run_cnt[i]);

	DHD_LB_STATS_CLR(dhd->txc_sched_cnt);

	dhd->txc_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->txc_percpu_run_cnt) {
		DHD_ERROR(("%s(): txc_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->txc_percpu_run_cnt[i]);

	dhd->cpu_online_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->cpu_online_cnt) {
		DHD_ERROR(("%s(): cpu_online_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->cpu_online_cnt[i]);

	dhd->cpu_offline_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->cpu_offline_cnt) {
		DHD_ERROR(("%s(): cpu_offline_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->cpu_offline_cnt[i]);

	dhd->txp_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->txp_percpu_run_cnt) {
		DHD_ERROR(("%s(): txp_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->txp_percpu_run_cnt[i]);

	dhd->tx_start_percpu_run_cnt = (uint32 *)MALLOC(dhdp->osh, alloc_size);
	if (!dhd->tx_start_percpu_run_cnt) {
		DHD_ERROR(("%s(): tx_start_percpu_run_cnt malloc failed \n",
			__FUNCTION__));
		return;
	}
	for (i = 0; i < num_cpus; i++)
		DHD_LB_STATS_CLR(dhd->tx_start_percpu_run_cnt[i]);

	for (j = 0; j < HIST_BIN_SIZE; j++) {
		dhd->napi_rx_hist[j] = (uint32 *)MALLOC(dhdp->osh, alloc_size);
		if (!dhd->napi_rx_hist[j]) {
			DHD_ERROR(("%s(): dhd->napi_rx_hist[%d] malloc failed \n",
				__FUNCTION__, j));
			return;
		}
		for (i = 0; i < num_cpus; i++) {
			DHD_LB_STATS_CLR(dhd->napi_rx_hist[j][i]);
		}
	}
#ifdef DHD_LB_TXC
	for (j = 0; j < HIST_BIN_SIZE; j++) {
		dhd->txc_hist[j] = (uint32 *)MALLOC(dhdp->osh, alloc_size);
		if (!dhd->txc_hist[j]) {
			DHD_ERROR(("%s(): dhd->txc_hist[%d] malloc failed \n",
			         __FUNCTION__, j));
			return;
		}
		for (i = 0; i < num_cpus; i++) {
			DHD_LB_STATS_CLR(dhd->txc_hist[j][i]);
		}
	}
#endif /* DHD_LB_TXC */
#ifdef DHD_LB_RXC
	for (j = 0; j < HIST_BIN_SIZE; j++) {
		dhd->rxc_hist[j] = (uint32 *)MALLOC(dhdp->osh, alloc_size);
		if (!dhd->rxc_hist[j]) {
			DHD_ERROR(("%s(): dhd->rxc_hist[%d] malloc failed \n",
				__FUNCTION__, j));
			return;
		}
		for (i = 0; i < num_cpus; i++) {
			DHD_LB_STATS_CLR(dhd->rxc_hist[j][i]);
		}
	}
#endif /* DHD_LB_RXC */
	return;
}

void dhd_lb_stats_deinit(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd;
	int j, num_cpus = num_possible_cpus();
	int alloc_size = sizeof(uint32) * num_cpus;

	if (dhdp == NULL) {
		DHD_ERROR(("%s(): Invalid argument dhd pubb pointer is NULL \n",
			__FUNCTION__));
		return;
	}

	dhd = dhdp->info;
	if (dhd == NULL) {
		DHD_ERROR(("%s(): DHD pointer is NULL \n", __FUNCTION__));
		return;
	}

	if (dhd->napi_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->napi_percpu_run_cnt, alloc_size);
		dhd->napi_percpu_run_cnt = NULL;
	}
	if (dhd->rxc_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->rxc_percpu_run_cnt, alloc_size);
		dhd->rxc_percpu_run_cnt = NULL;
	}
	if (dhd->txc_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->txc_percpu_run_cnt, alloc_size);
		dhd->txc_percpu_run_cnt = NULL;
	}
	if (dhd->cpu_online_cnt) {
		MFREE(dhdp->osh, dhd->cpu_online_cnt, alloc_size);
		dhd->cpu_online_cnt = NULL;
	}
	if (dhd->cpu_offline_cnt) {
		MFREE(dhdp->osh, dhd->cpu_offline_cnt, alloc_size);
		dhd->cpu_offline_cnt = NULL;
	}

	if (dhd->txp_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->txp_percpu_run_cnt, alloc_size);
		dhd->txp_percpu_run_cnt = NULL;
	}
	if (dhd->tx_start_percpu_run_cnt) {
		MFREE(dhdp->osh, dhd->tx_start_percpu_run_cnt, alloc_size);
		dhd->tx_start_percpu_run_cnt = NULL;
	}

	for (j = 0; j < HIST_BIN_SIZE; j++) {
		if (dhd->napi_rx_hist[j]) {
			MFREE(dhdp->osh, dhd->napi_rx_hist[j], alloc_size);
			dhd->napi_rx_hist[j] = NULL;
		}
#ifdef DHD_LB_TXC
		if (dhd->txc_hist[j]) {
			MFREE(dhdp->osh, dhd->txc_hist[j], alloc_size);
			dhd->txc_hist[j] = NULL;
		}
#endif /* DHD_LB_TXC */
#ifdef DHD_LB_RXC
		if (dhd->rxc_hist[j]) {
			MFREE(dhdp->osh, dhd->rxc_hist[j], alloc_size);
			dhd->rxc_hist[j] = NULL;
		}
#endif /* DHD_LB_RXC */
	}

	return;
}

void dhd_lb_stats_dump_histo(dhd_pub_t *dhdp,
	struct bcmstrbuf *strbuf, uint32 **hist)
{
	int i, j;
	uint32 *per_cpu_total;
	uint32 total = 0;
	uint32 num_cpus = num_possible_cpus();

	per_cpu_total = (uint32 *)MALLOC(dhdp->osh, sizeof(uint32) * num_cpus);
	if (!per_cpu_total) {
		DHD_ERROR(("%s(): dhd->per_cpu_total malloc failed \n", __FUNCTION__));
		return;
	}
	bzero(per_cpu_total, sizeof(uint32) * num_cpus);

	bcm_bprintf(strbuf, "CPU: \t\t");
	for (i = 0; i < num_cpus; i++)
		bcm_bprintf(strbuf, "%d\t", i);
	bcm_bprintf(strbuf, "\nBin\n");

	for (i = 0; i < HIST_BIN_SIZE; i++) {
		bcm_bprintf(strbuf, "%d:\t\t", 1<<i);
		for (j = 0; j < num_cpus; j++) {
			bcm_bprintf(strbuf, "%d\t", hist[i][j]);
		}
		bcm_bprintf(strbuf, "\n");
	}
	bcm_bprintf(strbuf, "Per CPU Total \t");
	total = 0;
	for (i = 0; i < num_cpus; i++) {
		for (j = 0; j < HIST_BIN_SIZE; j++) {
			per_cpu_total[i] += (hist[j][i] * (1<<j));
		}
		bcm_bprintf(strbuf, "%d\t", per_cpu_total[i]);
		total += per_cpu_total[i];
	}
	bcm_bprintf(strbuf, "\nTotal\t\t%d \n", total);

	if (per_cpu_total) {
		MFREE(dhdp->osh, per_cpu_total, sizeof(uint32) * num_cpus);
		per_cpu_total = NULL;
	}
	return;
}

void dhd_lb_stats_dump_cpu_array(struct bcmstrbuf *strbuf, uint32 *p)
{
	int i, num_cpus = num_possible_cpus();

	bcm_bprintf(strbuf, "CPU: \t");
	for (i = 0; i < num_cpus; i++)
		bcm_bprintf(strbuf, "%d\t", i);
	bcm_bprintf(strbuf, "\n");

	bcm_bprintf(strbuf, "Val: \t");
	for (i = 0; i < num_cpus; i++)
		bcm_bprintf(strbuf, "%u\t", *(p+i));
	bcm_bprintf(strbuf, "\n");
	return;
}

void dhd_lb_stats_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	dhd_info_t *dhd;

	if (dhdp == NULL || strbuf == NULL) {
		DHD_ERROR(("%s(): Invalid argument dhdp %p strbuf %p \n",
			__FUNCTION__, dhdp, strbuf));
		return;
	}

	dhd = dhdp->info;
	if (dhd == NULL) {
		DHD_ERROR(("%s(): DHD pointer is NULL \n", __FUNCTION__));
		return;
	}

	bcm_bprintf(strbuf, "\ncpu_online_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->cpu_online_cnt);

	bcm_bprintf(strbuf, "\ncpu_offline_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->cpu_offline_cnt);

	bcm_bprintf(strbuf, "\nsched_cnt: dhd_dpc %u napi %u rxc %u txc %u\n",
		dhd->dhd_dpc_cnt, dhd->napi_sched_cnt, dhd->rxc_sched_cnt,
		dhd->txc_sched_cnt);

#ifdef DHD_LB_RXP
	bcm_bprintf(strbuf, "\nnapi_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->napi_percpu_run_cnt);
	bcm_bprintf(strbuf, "\nNAPI Packets Received Histogram:\n");
	dhd_lb_stats_dump_histo(dhdp, strbuf, dhd->napi_rx_hist);
#endif /* DHD_LB_RXP */

#ifdef DHD_LB_RXC
	bcm_bprintf(strbuf, "\nrxc_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->rxc_percpu_run_cnt);
	bcm_bprintf(strbuf, "\nRX Completions (Buffer Post) Histogram:\n");
	dhd_lb_stats_dump_histo(dhdp, strbuf, dhd->rxc_hist);
#endif /* DHD_LB_RXC */

#ifdef DHD_LB_TXC
	bcm_bprintf(strbuf, "\ntxc_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->txc_percpu_run_cnt);
	bcm_bprintf(strbuf, "\nTX Completions (Buffer Free) Histogram:\n");
	dhd_lb_stats_dump_histo(dhdp, strbuf, dhd->txc_hist);
#endif /* DHD_LB_TXC */

#ifdef DHD_LB_TXP
	bcm_bprintf(strbuf, "\ntxp_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->txp_percpu_run_cnt);

	bcm_bprintf(strbuf, "\ntx_start_percpu_run_cnt:\n");
	dhd_lb_stats_dump_cpu_array(strbuf, dhd->tx_start_percpu_run_cnt);
#endif /* DHD_LB_TXP */
}

/* Given a number 'n' returns 'm' that is next larger power of 2 after n */
static inline uint32 next_larger_power2(uint32 num)
{
	num--;
	num |= (num >> 1);
	num |= (num >> 2);
	num |= (num >> 4);
	num |= (num >> 8);
	num |= (num >> 16);

	return (num + 1);
}

void dhd_lb_stats_update_histo(uint32 **bin, uint32 count, uint32 cpu)
{
	uint32 bin_power;
	uint32 *p;
	bin_power = next_larger_power2(count);

	switch (bin_power) {
		case   1: p = bin[0] + cpu; break;
		case   2: p = bin[1] + cpu; break;
		case   4: p = bin[2] + cpu; break;
		case   8: p = bin[3] + cpu; break;
		case  16: p = bin[4] + cpu; break;
		case  32: p = bin[5] + cpu; break;
		case  64: p = bin[6] + cpu; break;
		case 128: p = bin[7] + cpu; break;
		default : p = bin[8] + cpu; break;
	}

	*p = *p + 1;
	return;
}

void dhd_lb_stats_update_napi_histo(dhd_pub_t *dhdp, uint32 count)
{
	int cpu;
	dhd_info_t *dhd = dhdp->info;

	cpu = get_cpu();
	put_cpu();
	dhd_lb_stats_update_histo(dhd->napi_rx_hist, count, cpu);

	return;
}

void dhd_lb_stats_update_txc_histo(dhd_pub_t *dhdp, uint32 count)
{
	int cpu;
	dhd_info_t *dhd = dhdp->info;

	cpu = get_cpu();
	put_cpu();
	dhd_lb_stats_update_histo(dhd->txc_hist, count, cpu);

	return;
}

void dhd_lb_stats_update_rxc_histo(dhd_pub_t *dhdp, uint32 count)
{
	int cpu;
	dhd_info_t *dhd = dhdp->info;

	cpu = get_cpu();
	put_cpu();
	dhd_lb_stats_update_histo(dhd->rxc_hist, count, cpu);

	return;
}

void dhd_lb_stats_txc_percpu_cnt_incr(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	DHD_LB_STATS_PERCPU_ARR_INCR(dhd->txc_percpu_run_cnt);
}

void dhd_lb_stats_rxc_percpu_cnt_incr(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	DHD_LB_STATS_PERCPU_ARR_INCR(dhd->rxc_percpu_run_cnt);
}
#endif /* DHD_LB_STATS */

#endif /* DHD_LB */
#if defined(DHD_LB)
/**
 * dhd_tasklet_schedule - Function that runs in IPI context of the destination
 * CPU and schedules a tasklet.
 * @tasklet: opaque pointer to the tasklet
 */
INLINE void
dhd_tasklet_schedule(void *tasklet)
{
	tasklet_schedule((struct tasklet_struct *)tasklet);
}
/**
 * dhd_tasklet_schedule_on - Executes the passed takslet in a given CPU
 * @tasklet: tasklet to be scheduled
 * @on_cpu: cpu core id
 *
 * If the requested cpu is online, then an IPI is sent to this cpu via the
 * smp_call_function_single with no wait and the tasklet_schedule function
 * will be invoked to schedule the specified tasklet on the requested CPU.
 */
INLINE void
dhd_tasklet_schedule_on(struct tasklet_struct *tasklet, int on_cpu)
{
	const int wait = 0;
	smp_call_function_single(on_cpu,
		dhd_tasklet_schedule, (void *)tasklet, wait);
}

/**
 * dhd_work_schedule_on - Executes the passed work in a given CPU
 * @work: work to be scheduled
 * @on_cpu: cpu core id
 *
 * If the requested cpu is online, then an IPI is sent to this cpu via the
 * schedule_work_on and the work function
 * will be invoked to schedule the specified work on the requested CPU.
 */

INLINE void
dhd_work_schedule_on(struct work_struct *work, int on_cpu)
{
	schedule_work_on(on_cpu, work);
}

#if defined(DHD_LB_TXC)
/**
 * dhd_lb_tx_compl_dispatch - load balance by dispatching the tx_compl_tasklet
 * on another cpu. The tx_compl_tasklet will take care of DMA unmapping and
 * freeing the packets placed in the tx_compl workq
 */
void
dhd_lb_tx_compl_dispatch(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	int curr_cpu, on_cpu;

	if (dhd->rx_napi_netdev == NULL) {
		DHD_ERROR(("%s: dhd->rx_napi_netdev is NULL\n", __FUNCTION__));
		return;
	}

	DHD_LB_STATS_INCR(dhd->txc_sched_cnt);
	/*
	 * If the destination CPU is NOT online or is same as current CPU
	 * no need to schedule the work
	 */
	curr_cpu = get_cpu();
	put_cpu();

	on_cpu = atomic_read(&dhd->tx_compl_cpu);

	if ((on_cpu == curr_cpu) || (!cpu_online(on_cpu))) {
		dhd_tasklet_schedule(&dhd->tx_compl_tasklet);
	} else {
		schedule_work(&dhd->tx_compl_dispatcher_work);
	}
}

static void dhd_tx_compl_dispatcher_fn(struct work_struct * work)
{
	struct dhd_info *dhd =
		container_of(work, struct dhd_info, tx_compl_dispatcher_work);
	int cpu;

	get_online_cpus();
	cpu = atomic_read(&dhd->tx_compl_cpu);
	if (!cpu_online(cpu))
		dhd_tasklet_schedule(&dhd->tx_compl_tasklet);
	else
		dhd_tasklet_schedule_on(&dhd->tx_compl_tasklet, cpu);
	put_online_cpus();
}
#endif /* DHD_LB_TXC */

#if defined(DHD_LB_RXC)
/**
 * dhd_lb_rx_compl_dispatch - load balance by dispatching the rx_compl_tasklet
 * on another cpu. The rx_compl_tasklet will take care of reposting rx buffers
 * in the H2D RxBuffer Post common ring, by using the recycled pktids that were
 * placed in the rx_compl workq.
 *
 * @dhdp: pointer to dhd_pub object
 */
void
dhd_lb_rx_compl_dispatch(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	int curr_cpu, on_cpu;

	if (dhd->rx_napi_netdev == NULL) {
		DHD_ERROR(("%s: dhd->rx_napi_netdev is NULL\n", __FUNCTION__));
		return;
	}

	DHD_LB_STATS_INCR(dhd->rxc_sched_cnt);
	/*
	 * If the destination CPU is NOT online or is same as current CPU
	 * no need to schedule the work
	 */
	curr_cpu = get_cpu();
	put_cpu();
	on_cpu = atomic_read(&dhd->rx_compl_cpu);

	if ((on_cpu == curr_cpu) || (!cpu_online(on_cpu))) {
		dhd_tasklet_schedule(&dhd->rx_compl_tasklet);
	} else {
		schedule_work(&dhd->rx_compl_dispatcher_work);
	}
}

void dhd_rx_compl_dispatcher_fn(struct work_struct * work)
{
	struct dhd_info *dhd =
		container_of(work, struct dhd_info, rx_compl_dispatcher_work);
	int cpu;

	get_online_cpus();
	cpu = atomic_read(&dhd->rx_compl_cpu);
	if (!cpu_online(cpu))
		dhd_tasklet_schedule(&dhd->rx_compl_tasklet);
	else {
		dhd_tasklet_schedule_on(&dhd->rx_compl_tasklet, cpu);
	}
	put_online_cpus();
}
#endif /* DHD_LB_RXC */

#if defined(DHD_LB_TXP)
void dhd_tx_dispatcher_work(struct work_struct * work)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	struct dhd_info *dhd =
		container_of(work, struct dhd_info, tx_dispatcher_work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif
	dhd_tasklet_schedule(&dhd->tx_tasklet);
}

void dhd_tx_dispatcher_fn(dhd_pub_t *dhdp)
{
	int cpu;
	int net_tx_cpu;
	dhd_info_t *dhd = dhdp->info;

	preempt_disable();
	cpu = atomic_read(&dhd->tx_cpu);
	net_tx_cpu = atomic_read(&dhd->net_tx_cpu);

	/*
	 * Now if the NET_TX has pushed the packet in the same
	 * CPU that is chosen for Tx processing, seperate it out
	 * i.e run the TX processing tasklet in compl_cpu
	 */
	if (net_tx_cpu == cpu)
		cpu = atomic_read(&dhd->tx_compl_cpu);

	if (!cpu_online(cpu)) {
		/*
		 * Ooohh... but the Chosen CPU is not online,
		 * Do the job in the current CPU itself.
		 */
		dhd_tasklet_schedule(&dhd->tx_tasklet);
	} else {
		/*
		 * Schedule tx_dispatcher_work to on the cpu which
		 * in turn will schedule tx_tasklet.
		 */
		dhd_work_schedule_on(&dhd->tx_dispatcher_work, cpu);
	}
	preempt_enable();
}

/**
 * dhd_lb_tx_dispatch - load balance by dispatching the tx_tasklet
 * on another cpu. The tx_tasklet will take care of actually putting
 * the skbs into appropriate flow ring and ringing H2D interrupt
 *
 * @dhdp: pointer to dhd_pub object
 */
void
dhd_lb_tx_dispatch(dhd_pub_t *dhdp)
{
	dhd_info_t *dhd = dhdp->info;
	int curr_cpu;

	curr_cpu = get_cpu();
	put_cpu();

	/* Record the CPU in which the TX request from Network stack came */
	atomic_set(&dhd->net_tx_cpu, curr_cpu);

	/* Schedule the work to dispatch ... */
	dhd_tx_dispatcher_fn(dhdp);
}
#endif /* DHD_LB_TXP */

#if defined(DHD_LB_RXP)
/**
 * dhd_napi_poll - Load balance napi poll function to process received
 * packets and send up the network stack using netif_receive_skb()
 *
 * @napi: napi object in which context this poll function is invoked
 * @budget: number of packets to be processed.
 *
 * Fetch the dhd_info given the rx_napi_struct. Move all packets from the
 * rx_napi_queue into a local rx_process_queue (lock and queue move and unlock).
 * Dequeue each packet from head of rx_process_queue, fetch the ifid from the
 * packet tag and sendup.
 */
int
dhd_napi_poll(struct napi_struct *napi, int budget)
{
	int ifid;
	const int pkt_count = 1;
	const int chan = 0;
	struct sk_buff * skb;
	unsigned long flags;
	struct dhd_info *dhd;
	int processed = 0;
	struct sk_buff_head rx_process_queue;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	dhd = container_of(napi, struct dhd_info, rx_napi_struct);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif

	DHD_INFO(("%s napi_queue<%d> budget<%d>\n",
		__FUNCTION__, skb_queue_len(&dhd->rx_napi_queue), budget));
		__skb_queue_head_init(&rx_process_queue);

	/* extract the entire rx_napi_queue into local rx_process_queue */
	spin_lock_irqsave(&dhd->rx_napi_queue.lock, flags);
	skb_queue_splice_tail_init(&dhd->rx_napi_queue, &rx_process_queue);
	spin_unlock_irqrestore(&dhd->rx_napi_queue.lock, flags);

	while ((skb = __skb_dequeue(&rx_process_queue)) != NULL) {
		OSL_PREFETCH(skb->data);

		ifid = DHD_PKTTAG_IFID((dhd_pkttag_fr_t *)PKTTAG(skb));

		DHD_INFO(("%s dhd_rx_frame pkt<%p> ifid<%d>\n",
			__FUNCTION__, skb, ifid));

		dhd_rx_frame(&dhd->pub, ifid, skb, pkt_count, chan);
		processed++;
	}

	DHD_LB_STATS_UPDATE_NAPI_HISTO(&dhd->pub, processed);

	DHD_INFO(("%s processed %d\n", __FUNCTION__, processed));
	napi_complete(napi);

	return budget - 1;
}

/**
 * dhd_napi_schedule - Place the napi struct into the current cpus softnet napi
 * poll list. This function may be invoked via the smp_call_function_single
 * from a remote CPU.
 *
 * This function will essentially invoke __raise_softirq_irqoff(NET_RX_SOFTIRQ)
 * after the napi_struct is added to the softnet data's poll_list
 *
 * @info: pointer to a dhd_info struct
 */
static void
dhd_napi_schedule(void *info)
{
	dhd_info_t *dhd = (dhd_info_t *)info;

	DHD_INFO(("%s rx_napi_struct<%p> on cpu<%d>\n",
		__FUNCTION__, &dhd->rx_napi_struct, atomic_read(&dhd->rx_napi_cpu)));

	/* add napi_struct to softnet data poll list and raise NET_RX_SOFTIRQ */
	if (napi_schedule_prep(&dhd->rx_napi_struct)) {
		__napi_schedule(&dhd->rx_napi_struct);
#ifdef WAKEUP_KSOFTIRQD_POST_NAPI_SCHEDULE
		raise_softirq(NET_RX_SOFTIRQ);
#endif /* WAKEUP_KSOFTIRQD_POST_NAPI_SCHEDULE */
	}

	/*
	 * If the rx_napi_struct was already running, then we let it complete
	 * processing all its packets. The rx_napi_struct may only run on one
	 * core at a time, to avoid out-of-order handling.
	 */
}

/**
 * dhd_napi_schedule_on - API to schedule on a desired CPU core a NET_RX_SOFTIRQ
 * action after placing the dhd's rx_process napi object in the the remote CPU's
 * softnet data's poll_list.
 *
 * @dhd: dhd_info which has the rx_process napi object
 * @on_cpu: desired remote CPU id
 */
static INLINE int
dhd_napi_schedule_on(dhd_info_t *dhd, int on_cpu)
{
	int wait = 0; /* asynchronous IPI */
	DHD_INFO(("%s dhd<%p> napi<%p> on_cpu<%d>\n",
		__FUNCTION__, dhd, &dhd->rx_napi_struct, on_cpu));

	if (smp_call_function_single(on_cpu, dhd_napi_schedule, dhd, wait)) {
		DHD_ERROR(("%s smp_call_function_single on_cpu<%d> failed\n",
			__FUNCTION__, on_cpu));
	}

	DHD_LB_STATS_INCR(dhd->napi_sched_cnt);

	return 0;
}

/*
 * Call get_online_cpus/put_online_cpus around dhd_napi_schedule_on
 * Why should we do this?
 * The candidacy algorithm is run from the call back function
 * registered to CPU hotplug notifier. This call back happens from Worker
 * context. The dhd_napi_schedule_on is also from worker context.
 * Note that both of this can run on two different CPUs at the same time.
 * So we can possibly have a window where a given CPUn is being brought
 * down from CPUm while we try to run a function on CPUn.
 * To prevent this its better have the whole code to execute an SMP
 * function under get_online_cpus.
 * This function call ensures that hotplug mechanism does not kick-in
 * until we are done dealing with online CPUs
 * If the hotplug worker is already running, no worries because the
 * candidacy algo would then reflect the same in dhd->rx_napi_cpu.
 *
 * The below mentioned code structure is proposed in
 * https://www.kernel.org/doc/Documentation/cpu-hotplug.txt
 * for the question
 * Q: I need to ensure that a particular cpu is not removed when there is some
 *    work specific to this cpu is in progress
 *
 * According to the documentation calling get_online_cpus is NOT required, if
 * we are running from tasklet context. Since dhd_rx_napi_dispatcher_fn can
 * run from Work Queue context we have to call these functions
 */
void dhd_rx_napi_dispatcher_fn(struct work_struct * work)
{
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	struct dhd_info *dhd =
		container_of(work, struct dhd_info, rx_napi_dispatcher_work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif

	dhd_napi_schedule(dhd);
}

/**
 * dhd_lb_rx_napi_dispatch - load balance by dispatching the rx_napi_struct
 * to run on another CPU. The rx_napi_struct's poll function will retrieve all
 * the packets enqueued into the rx_napi_queue and sendup.
 * The producer's rx packet queue is appended to the rx_napi_queue before
 * dispatching the rx_napi_struct.
 */
void
dhd_lb_rx_napi_dispatch(dhd_pub_t *dhdp)
{
	unsigned long flags;
	dhd_info_t *dhd = dhdp->info;
	int curr_cpu;
	int on_cpu;
#ifdef DHD_LB_IRQSET
	cpumask_t cpus;
#endif /* DHD_LB_IRQSET */

	if (dhd->rx_napi_netdev == NULL) {
		DHD_ERROR(("%s: dhd->rx_napi_netdev is NULL\n", __FUNCTION__));
		return;
	}

	DHD_INFO(("%s append napi_queue<%d> pend_queue<%d>\n", __FUNCTION__,
		skb_queue_len(&dhd->rx_napi_queue), skb_queue_len(&dhd->rx_pend_queue)));

	/* append the producer's queue of packets to the napi's rx process queue */
	spin_lock_irqsave(&dhd->rx_napi_queue.lock, flags);
	skb_queue_splice_tail_init(&dhd->rx_pend_queue, &dhd->rx_napi_queue);
	spin_unlock_irqrestore(&dhd->rx_napi_queue.lock, flags);

	DHD_LB_STATS_PERCPU_ARR_INCR(dhd->napi_percpu_run_cnt);

	/* if LB RXP is disabled directly schedule NAPI */
	if (atomic_read(&dhd->lb_rxp_active) == 0) {
		dhd_napi_schedule(dhd);
		return;
	}

	/*
	 * If the destination CPU is NOT online or is same as current CPU
	 * no need to schedule the work
	 */
	curr_cpu = get_cpu();
	put_cpu();

	preempt_disable();
	on_cpu = atomic_read(&dhd->rx_napi_cpu);
#ifdef DHD_LB_IRQSET
	if (cpumask_and(&cpus, cpumask_of(curr_cpu), dhd->cpumask_primary) ||
			(!cpu_online(on_cpu)))
#else
	if ((on_cpu == curr_cpu) || (!cpu_online(on_cpu)))
#endif /* DHD_LB_IRQSET */
	{
		DHD_INFO(("%s : curr_cpu : %d, cpumask : 0x%lx\n", __FUNCTION__,
			curr_cpu, *cpumask_bits(dhd->cpumask_primary)));
		dhd_napi_schedule(dhd);
	} else {
		DHD_INFO(("%s : schedule to curr_cpu : %d, cpumask : 0x%lx\n",
			__FUNCTION__, curr_cpu, *cpumask_bits(dhd->cpumask_primary)));
		dhd_work_schedule_on(&dhd->rx_napi_dispatcher_work, on_cpu);
		DHD_LB_STATS_INCR(dhd->napi_sched_cnt);
	}
	preempt_enable();
}

/**
 * dhd_lb_rx_pkt_enqueue - Enqueue the packet into the producer's queue
 */
void
dhd_lb_rx_pkt_enqueue(dhd_pub_t *dhdp, void *pkt, int ifidx)
{
	dhd_info_t *dhd = dhdp->info;

	DHD_INFO(("%s enqueue pkt<%p> ifidx<%d> pend_queue<%d>\n", __FUNCTION__,
		pkt, ifidx, skb_queue_len(&dhd->rx_pend_queue)));
	DHD_PKTTAG_SET_IFID((dhd_pkttag_fr_t *)PKTTAG(pkt), ifidx);
	__skb_queue_tail(&dhd->rx_pend_queue, pkt);
}
#endif /* DHD_LB_RXP */
#endif /* DHD_LB */

#if defined(DHD_LB_IRQSET) || defined(DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON)
void
dhd_irq_set_affinity(dhd_pub_t *dhdp, const struct cpumask *cpumask)
{
	unsigned int irq = (unsigned int)-1;
	int err = BCME_OK;

	if (!dhdp) {
		DHD_ERROR(("%s : dhdp is NULL\n", __FUNCTION__));
		return;
	}

	if (!dhdp->bus) {
		DHD_ERROR(("%s : bus is NULL\n", __FUNCTION__));
		return;
	}

	DHD_ERROR(("%s : irq set affinity cpu:0x%lx\n",
			__FUNCTION__, *cpumask_bits(cpumask)));

	dhdpcie_get_pcieirq(dhdp->bus, &irq);
	err = irq_set_affinity(irq, cpumask);
	if (err)
		DHD_ERROR(("%s : irq set affinity is failed cpu:0x%lx\n",
			__FUNCTION__, *cpumask_bits(cpumask)));
}
#endif /* DHD_LB_IRQSET || DHD_CONTROL_PCIE_CPUCORE_WIFI_TURNON */

#if defined(DHD_LB_TXP)

int BCMFASTPATH
dhd_lb_sendpkt(dhd_info_t *dhd, struct net_device *net,
	int ifidx, void *skb)
{
	DHD_LB_STATS_PERCPU_ARR_INCR(dhd->tx_start_percpu_run_cnt);

	/* If the feature is disabled run-time do TX from here */
	if (atomic_read(&dhd->lb_txp_active) == 0) {
		DHD_LB_STATS_PERCPU_ARR_INCR(dhd->txp_percpu_run_cnt);
		 return __dhd_sendpkt(&dhd->pub, ifidx, skb);
	}

	/* Store the address of net device and interface index in the Packet tag */
	DHD_LB_TX_PKTTAG_SET_NETDEV((dhd_tx_lb_pkttag_fr_t *)PKTTAG(skb), net);
	DHD_LB_TX_PKTTAG_SET_IFIDX((dhd_tx_lb_pkttag_fr_t *)PKTTAG(skb), ifidx);

	/* Enqueue the skb into tx_pend_queue */
	skb_queue_tail(&dhd->tx_pend_queue, skb);

	DHD_TRACE(("%s(): Added skb %p for netdev %p \r\n", __FUNCTION__, skb, net));

	/* Dispatch the Tx job to be processed by the tx_tasklet */
	dhd_lb_tx_dispatch(&dhd->pub);

	return NETDEV_TX_OK;
}
#endif /* DHD_LB_TXP */

#ifdef DHD_LB_TXP
#define DHD_LB_TXBOUND	64
/*
 * Function that performs the TX processing on a given CPU
 */
bool
dhd_lb_tx_process(dhd_info_t *dhd)
{
	struct sk_buff *skb;
	int cnt = 0;
	struct net_device *net;
	int ifidx;
	bool resched = FALSE;

	DHD_TRACE(("%s(): TX Processing \r\n", __FUNCTION__));
	if (dhd == NULL) {
		DHD_ERROR((" Null pointer DHD \r\n"));
		return resched;
	}

	BCM_REFERENCE(net);

	DHD_LB_STATS_PERCPU_ARR_INCR(dhd->txp_percpu_run_cnt);

	/* Base Loop to perform the actual Tx */
	do {
		skb = skb_dequeue(&dhd->tx_pend_queue);
		if (skb == NULL) {
			DHD_TRACE(("Dequeued a Null Packet \r\n"));
			break;
		}
		cnt++;

		net =  DHD_LB_TX_PKTTAG_NETDEV((dhd_tx_lb_pkttag_fr_t *)PKTTAG(skb));
		ifidx = DHD_LB_TX_PKTTAG_IFIDX((dhd_tx_lb_pkttag_fr_t *)PKTTAG(skb));

		DHD_TRACE(("Processing skb %p for net %p index %d \r\n", skb,
			net, ifidx));

		__dhd_sendpkt(&dhd->pub, ifidx, skb);

		if (cnt >= DHD_LB_TXBOUND) {
			resched = TRUE;
			break;
		}

	} while (1);

	DHD_INFO(("%s(): Processed %d packets \r\n", __FUNCTION__, cnt));

	return resched;
}

void
dhd_lb_tx_handler(unsigned long data)
{
	dhd_info_t *dhd = (dhd_info_t *)data;

	if (dhd_lb_tx_process(dhd)) {
		dhd_tasklet_schedule(&dhd->tx_tasklet);
	}
}

#endif /* DHD_LB_TXP */
