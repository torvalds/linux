/*
 * MPC5121 Prototypes and definitions
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.
 */

#ifndef __ASM_POWERPC_MPC5121_H__
#define __ASM_POWERPC_MPC5121_H__

/* MPC512x Reset module registers */
struct mpc512x_reset_module {
	u32	rcwlr;	/* Reset Configuration Word Low Register */
	u32	rcwhr;	/* Reset Configuration Word High Register */
	u32	reserved1;
	u32	reserved2;
	u32	rsr;	/* Reset Status Register */
	u32	rmr;	/* Reset Mode Register */
	u32	rpr;	/* Reset Protection Register */
	u32	rcr;	/* Reset Control Register */
	u32	rcer;	/* Reset Control Enable Register */
};

/*
 * Clock Control Module
 */
struct mpc512x_ccm {
	u32	spmr;	/* System PLL Mode Register */
	u32	sccr1;	/* System Clock Control Register 1 */
	u32	sccr2;	/* System Clock Control Register 2 */
	u32	scfr1;	/* System Clock Frequency Register 1 */
	u32	scfr2;	/* System Clock Frequency Register 2 */
	u32	scfr2s;	/* System Clock Frequency Shadow Register 2 */
	u32	bcr;	/* Bread Crumb Register */
	u32	psc_ccr[12];	/* PSC Clock Control Registers */
	u32	spccr;	/* SPDIF Clock Control Register */
	u32	cccr;	/* CFM Clock Control Register */
	u32	dccr;	/* DIU Clock Control Register */
	u32	mscan_ccr[4];	/* MSCAN Clock Control Registers */
	u32	out_ccr[4];	/* OUT CLK Configure Registers */
	u32	rsv0[2];	/* Reserved */
	u32	scfr3;		/* System Clock Frequency Register 3 */
	u32	rsv1[3];	/* Reserved */
	u32	spll_lock_cnt;	/* System PLL Lock Counter */
	u8	res[0x6c];	/* Reserved */
};

/*
 * LPC Module
 */
struct mpc512x_lpc {
	u32	cs_cfg[8];	/* CS config */
	u32	cs_ctrl;	/* CS Control Register */
	u32	cs_status;	/* CS Status Register */
	u32	burst_ctrl;	/* CS Burst Control Register */
	u32	deadcycle_ctrl;	/* CS Deadcycle Control Register */
	u32	holdcycle_ctrl;	/* CS Holdcycle Control Register */
	u32	alt;		/* Address Latch Timing Register */
};

int mpc512x_cs_config(unsigned int cs, u32 val);

/*
 * SCLPC Module (LPB FIFO)
 */
struct mpc512x_lpbfifo {
	u32	pkt_size;	/* SCLPC Packet Size Register */
	u32	start_addr;	/* SCLPC Start Address Register */
	u32	ctrl;		/* SCLPC Control Register */
	u32	enable;		/* SCLPC Enable Register */
	u32	reserved1;
	u32	status;		/* SCLPC Status Register */
	u32	bytes_done;	/* SCLPC Bytes Done Register */
	u32	emb_sc;		/* EMB Share Counter Register */
	u32	emb_pc;		/* EMB Pause Control Register */
	u32	reserved2[7];
	u32	data_word;	/* LPC RX/TX FIFO Data Word Register */
	u32	fifo_status;	/* LPC RX/TX FIFO Status Register */
	u32	fifo_ctrl;	/* LPC RX/TX FIFO Control Register */
	u32	fifo_alarm;	/* LPC RX/TX FIFO Alarm Register */
};

#define MPC512X_SCLPC_START		(1 << 31)
#define MPC512X_SCLPC_CS(x)		(((x) & 0x7) << 24)
#define MPC512X_SCLPC_FLUSH		(1 << 17)
#define MPC512X_SCLPC_READ		(1 << 16)
#define MPC512X_SCLPC_DAI		(1 << 8)
#define MPC512X_SCLPC_BPT(x)		((x) & 0x3f)
#define MPC512X_SCLPC_RESET		(1 << 24)
#define MPC512X_SCLPC_FIFO_RESET	(1 << 16)
#define MPC512X_SCLPC_ABORT_INT_ENABLE	(1 << 9)
#define MPC512X_SCLPC_NORM_INT_ENABLE	(1 << 8)
#define MPC512X_SCLPC_ENABLE		(1 << 0)
#define MPC512X_SCLPC_SUCCESS		(1 << 24)
#define MPC512X_SCLPC_FIFO_CTRL(x)	(((x) & 0x7) << 24)
#define MPC512X_SCLPC_FIFO_ALARM(x)	((x) & 0x3ff)

enum lpb_dev_portsize {
	LPB_DEV_PORTSIZE_UNDEFINED = 0,
	LPB_DEV_PORTSIZE_1_BYTE = 1,
	LPB_DEV_PORTSIZE_2_BYTES = 2,
	LPB_DEV_PORTSIZE_4_BYTES = 4,
	LPB_DEV_PORTSIZE_8_BYTES = 8
};

enum mpc512x_lpbfifo_req_dir {
	MPC512X_LPBFIFO_REQ_DIR_READ,
	MPC512X_LPBFIFO_REQ_DIR_WRITE
};

struct mpc512x_lpbfifo_request {
	phys_addr_t dev_phys_addr; /* physical address of some device on LPB */
	void *ram_virt_addr; /* virtual address of some region in RAM */
	u32 size;
	enum lpb_dev_portsize portsize;
	enum mpc512x_lpbfifo_req_dir dir;
	void (*callback)(struct mpc512x_lpbfifo_request *);
};

int mpc512x_lpbfifo_submit(struct mpc512x_lpbfifo_request *req);

#endif /* __ASM_POWERPC_MPC5121_H__ */
