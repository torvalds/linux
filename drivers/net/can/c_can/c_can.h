/*
 * CAN bus driver for Bosch C_CAN controller
 *
 * Copyright (C) 2010 ST Microelectronics
 * Bhupesh Sharma <bhupesh.sharma@st.com>
 *
 * Borrowed heavily from the C_CAN driver originally written by:
 * Copyright (C) 2007
 * - Sascha Hauer, Marc Kleine-Budde, Pengutronix <s.hauer@pengutronix.de>
 * - Simon Kallweit, intefo AG <simon.kallweit@intefo.ch>
 *
 * Bosch C_CAN controller is compliant to CAN protocol version 2.0 part A and B.
 * Bosch C_CAN user manual can be obtained from:
 * http://www.semiconductors.bosch.de/media/en/pdf/ipmodules_1/c_can/
 * users_manual_c_can.pdf
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef C_CAN_H
#define C_CAN_H

/* c_can IF registers */
struct c_can_if_regs {
	u16 com_req;
	u16 com_mask;
	u16 mask1;
	u16 mask2;
	u16 arb1;
	u16 arb2;
	u16 msg_cntrl;
	u16 data[4];
	u16 _reserved[13];
};

/* c_can hardware registers */
struct c_can_regs {
	u16 control;
	u16 status;
	u16 err_cnt;
	u16 btr;
	u16 interrupt;
	u16 test;
	u16 brp_ext;
	u16 _reserved1;
	struct c_can_if_regs ifregs[2]; /* [0] = IF1 and [1] = IF2 */
	u16 _reserved2[8];
	u16 txrqst1;
	u16 txrqst2;
	u16 _reserved3[6];
	u16 newdat1;
	u16 newdat2;
	u16 _reserved4[6];
	u16 intpnd1;
	u16 intpnd2;
	u16 _reserved5[6];
	u16 msgval1;
	u16 msgval2;
	u16 _reserved6[6];
};

/* c_can private data structure */
struct c_can_priv {
	struct can_priv can;	/* must be the first member */
	struct napi_struct napi;
	struct net_device *dev;
	int tx_object;
	int current_status;
	int last_status;
	u16 (*read_reg) (struct c_can_priv *priv, void *reg);
	void (*write_reg) (struct c_can_priv *priv, void *reg, u16 val);
	struct c_can_regs __iomem *regs;
	unsigned long irq_flags; /* for request_irq() */
	unsigned int tx_next;
	unsigned int tx_echo;
	void *priv;		/* for board-specific data */
};

struct net_device *alloc_c_can_dev(void);
void free_c_can_dev(struct net_device *dev);
int register_c_can_dev(struct net_device *dev);
void unregister_c_can_dev(struct net_device *dev);

#endif /* C_CAN_H */
