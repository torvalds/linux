/*
 * TVIN Decoder
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __TVIN_DECODER_H
#define __TVIN_DECODER_H

/* Standard Linux Headers */
#include <linux/list.h>

/* Amlogic Headers */
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/tvin/tvin.h>

/* Local Headers */
#include "tvin_global.h"

struct tvin_frontend_s;


typedef struct tvin_decoder_ops_s {
        /*
         * check whether the port is supported.
         * return 0 if not supported, return other if supported.
         */
        int  (*support)        (struct tvin_frontend_s *fe, enum tvin_port_e port);
        int  (*open)           (struct tvin_frontend_s *fe, enum tvin_port_e port);
        void (*start)          (struct tvin_frontend_s *fe, enum tvin_sig_fmt_e fmt);
        void (*stop)           (struct tvin_frontend_s *fe, enum tvin_port_e port);
        void (*close)          (struct tvin_frontend_s *fe);
        int  (*decode_isr)     (struct tvin_frontend_s *fe, unsigned int hcnt64);
	int  (*callmaster_det) (enum tvin_port_e port,struct tvin_frontend_s *fe);
	int  (*ioctl)          (struct tvin_frontend_s *fe, void *args);
        int  (*decode_tsk)    (struct tvin_frontend_s *fe);
} tvin_decoder_ops_t;

typedef struct tvin_state_machine_ops_s {
        bool (*nosig) (struct tvin_frontend_s *fe);
        bool (*fmt_changed) (struct tvin_frontend_s *fe);
        enum tvin_sig_fmt_e (*get_fmt) (struct tvin_frontend_s *fe);
        void (*fmt_config)(struct tvin_frontend_s *fe);
        bool (*adc_cal)(struct tvin_frontend_s *fe);
        bool (*pll_lock) (struct tvin_frontend_s *fe);
        void (*get_sig_propery)(struct tvin_frontend_s *fe, struct tvin_sig_property_s *prop);
        void (*vga_set_param)(struct tvafe_vga_parm_s *vga_parm, struct tvin_frontend_s *fe);
        void (*vga_get_param)(struct tvafe_vga_parm_s *vga_parm, struct tvin_frontend_s *fe);
        bool (*check_frame_skip)(struct tvin_frontend_s *fe);
        bool (*get_secam_phase)(struct tvin_frontend_s *fe);
} tvin_state_machine_ops_t;

typedef struct tvin_frontend_s {
        int index; /* support multi-frontend of same decoder */
        char name[15]; /* just name of frontend, not port name or format name */
        int port; /* current port */
        struct tvin_decoder_ops_s *dec_ops;
        struct tvin_state_machine_ops_s *sm_ops;
        unsigned int flag;
        void *private_data;
        unsigned reserved;
        struct list_head list;
} tvin_frontend_t;


int tvin_frontend_init(tvin_frontend_t *fe,
                tvin_decoder_ops_t *dec_ops, tvin_state_machine_ops_t *sm_ops, int index);
int tvin_reg_frontend(struct tvin_frontend_s *fe);
void tvin_unreg_frontend(struct tvin_frontend_s *fe);
struct tvin_frontend_s * tvin_get_frontend(enum tvin_port_e port, int index);
struct tvin_decoder_ops_s *tvin_get_fe_ops(enum tvin_port_e port, int index);
struct tvin_state_machine_ops_s *tvin_get_sm_ops(enum tvin_port_e port, int index);

#endif

