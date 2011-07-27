/* Copyright (C) 2007,2008 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#undef DEBUG
#undef VERBOSE

#ifdef DEBUG
#define DBG(fmt, args...) printk(KERN_DEBUG "[%s]  " fmt , \
				 __func__, ## args)
#else
#define DBG(fmt, args...)	do {} while (0)
#endif

#ifdef VERBOSE
#define VDBG		DBG
#else
#define VDBG(stuff...)	do {} while (0)
#endif

#ifdef VERBOSE
#define MPC_LOC printk("Current Location [%s]:[%d]\n", __FILE__, __LINE__)
#else
#define MPC_LOC do {} while (0)
#endif

#define PROTO_UNDEF	(0)
#define PROTO_HOST	(1)
#define PROTO_GADGET	(2)

/* OTG state machine according to the OTG spec */
struct otg_fsm {
	/* Input */
	int a_bus_resume;
	int a_bus_suspend;
	int a_conn;
	int a_sess_vld;
	int a_srp_det;
	int a_vbus_vld;
	int b_bus_resume;
	int b_bus_suspend;
	int b_conn;
	int b_se0_srp;
	int b_sess_end;
	int b_sess_vld;
	int id;

	/* Internal variables */
	int a_set_b_hnp_en;
	int b_srp_done;
	int b_hnp_enable;

	/* Timeout indicator for timers */
	int a_wait_vrise_tmout;
	int a_wait_bcon_tmout;
	int a_aidl_bdis_tmout;
	int b_ase0_brst_tmout;

	/* Informative variables */
	int a_bus_drop;
	int a_bus_req;
	int a_clr_err;
	int a_suspend_req;
	int b_bus_req;

	/* Output */
	int drv_vbus;
	int loc_conn;
	int loc_sof;

	struct otg_fsm_ops *ops;
	struct otg_transceiver *transceiver;

	/* Current usb protocol used: 0:undefine; 1:host; 2:client */
	int protocol;
	spinlock_t lock;
};

struct otg_fsm_ops {
	void	(*chrg_vbus)(int on);
	void	(*drv_vbus)(int on);
	void	(*loc_conn)(int on);
	void	(*loc_sof)(int on);
	void	(*start_pulse)(void);
	void	(*add_timer)(void *timer);
	void	(*del_timer)(void *timer);
	int	(*start_host)(struct otg_fsm *fsm, int on);
	int	(*start_gadget)(struct otg_fsm *fsm, int on);
};


static inline void otg_chrg_vbus(struct otg_fsm *fsm, int on)
{
	fsm->ops->chrg_vbus(on);
}

static inline void otg_drv_vbus(struct otg_fsm *fsm, int on)
{
	if (fsm->drv_vbus != on) {
		fsm->drv_vbus = on;
		fsm->ops->drv_vbus(on);
	}
}

static inline void otg_loc_conn(struct otg_fsm *fsm, int on)
{
	if (fsm->loc_conn != on) {
		fsm->loc_conn = on;
		fsm->ops->loc_conn(on);
	}
}

static inline void otg_loc_sof(struct otg_fsm *fsm, int on)
{
	if (fsm->loc_sof != on) {
		fsm->loc_sof = on;
		fsm->ops->loc_sof(on);
	}
}

static inline void otg_start_pulse(struct otg_fsm *fsm)
{
	fsm->ops->start_pulse();
}

static inline void otg_add_timer(struct otg_fsm *fsm, void *timer)
{
	fsm->ops->add_timer(timer);
}

static inline void otg_del_timer(struct otg_fsm *fsm, void *timer)
{
	fsm->ops->del_timer(timer);
}

int otg_statemachine(struct otg_fsm *fsm);

/* Defined by device specific driver, for different timer implementation */
extern struct fsl_otg_timer *a_wait_vrise_tmr, *a_wait_bcon_tmr,
	*a_aidl_bdis_tmr, *b_ase0_brst_tmr, *b_se0_srp_tmr, *b_srp_fail_tmr,
	*a_wait_enum_tmr;
