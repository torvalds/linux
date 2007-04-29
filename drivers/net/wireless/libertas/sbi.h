/**
  * This file contains IF layer definitions.
  */

#ifndef	_SBI_H_
#define	_SBI_H_

#include <linux/interrupt.h>

#include "defs.h"

/** INT status Bit Definition*/
#define his_cmddnldrdy			0x01
#define his_cardevent			0x02
#define his_cmdupldrdy			0x04

#ifndef DEV_NAME_LEN
#define DEV_NAME_LEN			32
#endif

#define SBI_EVENT_CAUSE_SHIFT		3

/* Probe and Check if the card is present*/
int libertas_sbi_register_dev(wlan_private * priv);
int libertas_sbi_unregister_dev(wlan_private *);
int libertas_sbi_get_int_status(wlan_private * priv, u8 *);
int libertas_sbi_register(void);
void libertas_sbi_unregister(void);
int libertas_sbi_prog_firmware(wlan_private *);

int libertas_sbi_read_event_cause(wlan_private *);
int libertas_sbi_host_to_card(wlan_private * priv, u8 type, u8 * payload, u16 nb);
wlan_private *libertas_sbi_get_priv(void *card);

#ifdef ENABLE_PM
int libertas_sbi_suspend(wlan_private *);
int libertas_sbi_resume(wlan_private *);
#endif

#endif				/* _SBI_H */
