/*
 * Copyright Altera Corporation (C) 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __SOCFPGA_CTI_H
#define __SOCFPGA_CTI_H

#define CTI_MPU_IRQ_TRIG_IN	1
#define CTI_MPU_IRQ_TRIG_OUT	6

#define PMU_CHANNEL_0	0
#define PMU_CHANNEL_1	1

#ifdef CONFIG_HW_PERF_EVENTS
extern irqreturn_t socfpga_pmu_handler(int irq, void *dev, irq_handler_t handler);
extern int socfpga_init_cti(struct platform_device *pdev);
extern int socfpga_start_cti(struct platform_device *pdev);
extern int socfpga_stop_cti(struct platform_device *pdev);
#endif
#endif /* __SOCFPGA_CTI_H */
