/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * gp8psk_fe driver
 */

#ifndef GP8PSK_FE_H
#define GP8PSK_FE_H

#include <linux/types.h>

/* gp8psk commands */

#define GET_8PSK_CONFIG                 0x80    /* in */
#define SET_8PSK_CONFIG                 0x81
#define I2C_WRITE			0x83
#define I2C_READ			0x84
#define ARM_TRANSFER                    0x85
#define TUNE_8PSK                       0x86
#define GET_SIGNAL_STRENGTH             0x87    /* in */
#define LOAD_BCM4500                    0x88
#define BOOT_8PSK                       0x89    /* in */
#define START_INTERSIL                  0x8A    /* in */
#define SET_LNB_VOLTAGE                 0x8B
#define SET_22KHZ_TONE                  0x8C
#define SEND_DISEQC_COMMAND             0x8D
#define SET_DVB_MODE                    0x8E
#define SET_DN_SWITCH                   0x8F
#define GET_SIGNAL_LOCK                 0x90    /* in */
#define GET_FW_VERS			0x92
#define GET_SERIAL_NUMBER               0x93    /* in */
#define USE_EXTRA_VOLT                  0x94
#define GET_FPGA_VERS			0x95
#define CW3K_INIT			0x9d

/* PSK_configuration bits */
#define bm8pskStarted                   0x01
#define bm8pskFW_Loaded                 0x02
#define bmIntersilOn                    0x04
#define bmDVBmode                       0x08
#define bm22kHz                         0x10
#define bmSEL18V                        0x20
#define bmDCtuned                       0x40
#define bmArmed                         0x80

/* Satellite modulation modes */
#define ADV_MOD_DVB_QPSK 0     /* DVB-S QPSK */
#define ADV_MOD_TURBO_QPSK 1   /* Turbo QPSK */
#define ADV_MOD_TURBO_8PSK 2   /* Turbo 8PSK (also used for Trellis 8PSK) */
#define ADV_MOD_TURBO_16QAM 3  /* Turbo 16QAM (also used for Trellis 8PSK) */

#define ADV_MOD_DCII_C_QPSK 4  /* Digicipher II Combo */
#define ADV_MOD_DCII_I_QPSK 5  /* Digicipher II I-stream */
#define ADV_MOD_DCII_Q_QPSK 6  /* Digicipher II Q-stream */
#define ADV_MOD_DCII_C_OQPSK 7 /* Digicipher II offset QPSK */
#define ADV_MOD_DSS_QPSK 8     /* DSS (DIRECTV) QPSK */
#define ADV_MOD_DVB_BPSK 9     /* DVB-S BPSK */

/* firmware revision id's */
#define GP8PSK_FW_REV1			0x020604
#define GP8PSK_FW_REV2			0x020704
#define GP8PSK_FW_VERS(_fw_vers) \
	((_fw_vers)[2]<<0x10 | (_fw_vers)[1]<<0x08 | (_fw_vers)[0])

struct gp8psk_fe_ops {
	int (*in)(void *priv, u8 req, u16 value, u16 index, u8 *b, int blen);
	int (*out)(void *priv, u8 req, u16 value, u16 index, u8 *b, int blen);
	int (*reload)(void *priv);
};

struct dvb_frontend *gp8psk_fe_attach(const struct gp8psk_fe_ops *ops,
				      void *priv, bool is_rev1);

#endif
