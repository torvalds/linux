/* SPDX-License-Identifier: GPL-2.0-or-later */
/*******************************************************************************
 *
 * CTU CAN FD IP Core
 *
 * Copyright (C) 2015-2018 Ondrej Ille <ondrej.ille@gmail.com> FEE CTU
 * Copyright (C) 2018-2021 Ondrej Ille <ondrej.ille@gmail.com> self-funded
 * Copyright (C) 2018-2019 Martin Jerabek <martin.jerabek01@gmail.com> FEE CTU
 * Copyright (C) 2018-2021 Pavel Pisa <pisa@cmp.felk.cvut.cz> FEE CTU/self-funded
 *
 * Project advisors:
 *     Jiri Novak <jnovak@fel.cvut.cz>
 *     Pavel Pisa <pisa@cmp.felk.cvut.cz>
 *
 * Department of Measurement         (http://meas.fel.cvut.cz/)
 * Faculty of Electrical Engineering (http://www.fel.cvut.cz)
 * Czech Technical University        (http://www.cvut.cz/)
 ******************************************************************************/

#ifndef __CTUCANFD__
#define __CTUCANFD__

#include <linux/netdevice.h>
#include <linux/can/dev.h>
#include <linux/list.h>

enum ctu_can_fd_can_registers;

struct ctucan_priv {
	struct can_priv can; /* must be first member! */

	void __iomem *mem_base;
	u32 (*read_reg)(struct ctucan_priv *priv,
			enum ctu_can_fd_can_registers reg);
	void (*write_reg)(struct ctucan_priv *priv,
			  enum ctu_can_fd_can_registers reg, u32 val);

	unsigned int txb_head;
	unsigned int txb_tail;
	u32 txb_prio;
	unsigned int ntxbufs;
	spinlock_t tx_lock; /* spinlock to serialize allocation and processing of TX buffers */

	struct napi_struct napi;
	struct device *dev;
	struct clk *can_clk;

	int irq_flags;
	unsigned long drv_flags;

	u32 rxfrm_first_word;

	struct list_head peers_on_pdev;
};

/**
 * ctucan_probe_common - Device type independent registration call
 *
 * This function does all the memory allocation and registration for the CAN
 * device.
 *
 * @dev:	Handle to the generic device structure
 * @addr:	Base address of CTU CAN FD core address
 * @irq:	Interrupt number
 * @ntxbufs:	Number of implemented Tx buffers
 * @can_clk_rate: Clock rate, if 0 then clock are taken from device node
 * @pm_enable_call: Whether pm_runtime_enable should be called
 * @set_drvdata_fnc: Function to set network driver data for physical device
 *
 * Return: 0 on success and failure value on error
 */
int ctucan_probe_common(struct device *dev, void __iomem *addr,
			int irq, unsigned int ntxbufs,
			unsigned long can_clk_rate,
			int pm_enable_call,
			void (*set_drvdata_fnc)(struct device *dev,
						struct net_device *ndev));

int ctucan_suspend(struct device *dev) __maybe_unused;
int ctucan_resume(struct device *dev) __maybe_unused;

#endif /*__CTUCANFD__*/
