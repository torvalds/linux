/* SPDX-License-Identifier: GPL-2.0-or-later */
/****************************************************************

Siano Mobile Silicon, Inc.
MDTV receiver kernel modules.
Copyright (C) 2006-2009, Uri Shkolnik


****************************************************************/

#ifndef __SMS_ENDIAN_H__
#define __SMS_ENDIAN_H__

#include <asm/byteorder.h>

extern void smsendian_handle_tx_message(void *buffer);
extern void smsendian_handle_rx_message(void *buffer);
extern void smsendian_handle_message_header(void *msg);

#endif /* __SMS_ENDIAN_H__ */

