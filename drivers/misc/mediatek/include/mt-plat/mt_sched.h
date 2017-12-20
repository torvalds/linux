/*
* Copyright (C) 2016 MediaTek Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See http://www.gnu.org/licenses/gpl-2.0.html for more details.
*/

#ifdef CONFIG_MTK_SCHED_RQAVG_US
/*
 * @cpu: cpu id
 * @reset: reset the statistic start time after this time query
 * @use_maxfreq: caculate cpu loading with max cpu max frequency
 * return: cpu loading as percentage (0~100)
 */
extern unsigned int sched_get_percpu_load(int cpu, bool reset, bool use_maxfreq);

/*
 * return: heavy task(loading>90%) number in the system
 */
extern unsigned int sched_get_nr_heavy_task(void);

/*
 * @threshold: heavy task loading threshold (0~1023)
 * return: heavy task(loading>threshold) number in the system
 */
extern unsigned int sched_get_nr_heavy_task_by_threshold(unsigned int threshold);
#endif /* CONFIG_MTK_SCHED_RQAVG_US */

