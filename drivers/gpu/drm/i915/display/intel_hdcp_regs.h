/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_HDCP_REGS_H__
#define __INTEL_HDCP_REGS_H__

#include "intel_display_reg_defs.h"

/* HDCP Key Registers */
#define HDCP_KEY_CONF			_MMIO(0x66c00)
#define  HDCP_AKSV_SEND_TRIGGER		REG_BIT(31)
#define  HDCP_CLEAR_KEYS_TRIGGER	REG_BIT(30)
#define  HDCP_KEY_LOAD_TRIGGER		REG_BIT(8)
#define HDCP_KEY_STATUS			_MMIO(0x66c04)
#define  HDCP_FUSE_IN_PROGRESS		REG_BIT(7)
#define  HDCP_FUSE_ERROR		REG_BIT(6)
#define  HDCP_FUSE_DONE			REG_BIT(5)
#define  HDCP_KEY_LOAD_STATUS		REG_BIT(1)
#define  HDCP_KEY_LOAD_DONE		REG_BIT(0)
#define HDCP_AKSV_LO			_MMIO(0x66c10)
#define HDCP_AKSV_HI			_MMIO(0x66c14)

/* HDCP Repeater Registers */
#define HDCP_REP_CTL			_MMIO(0x66d00)
#define  HDCP_TRANSA_REP_PRESENT	REG_BIT(31)
#define  HDCP_TRANSB_REP_PRESENT	REG_BIT(30)
#define  HDCP_TRANSC_REP_PRESENT	REG_BIT(29)
#define  HDCP_TRANSD_REP_PRESENT	REG_BIT(28)
#define  HDCP_DDIB_REP_PRESENT		REG_BIT(30)
#define  HDCP_DDIA_REP_PRESENT		REG_BIT(29)
#define  HDCP_DDIC_REP_PRESENT		REG_BIT(28)
#define  HDCP_DDID_REP_PRESENT		REG_BIT(27)
#define  HDCP_DDIF_REP_PRESENT		REG_BIT(26)
#define  HDCP_DDIE_REP_PRESENT		REG_BIT(25)
#define  HDCP_TRANSA_SHA1_M0		(1 << 20)
#define  HDCP_TRANSB_SHA1_M0		(2 << 20)
#define  HDCP_TRANSC_SHA1_M0		(3 << 20)
#define  HDCP_TRANSD_SHA1_M0		(4 << 20)
#define  HDCP_DDIB_SHA1_M0		(1 << 20)
#define  HDCP_DDIA_SHA1_M0		(2 << 20)
#define  HDCP_DDIC_SHA1_M0		(3 << 20)
#define  HDCP_DDID_SHA1_M0		(4 << 20)
#define  HDCP_DDIF_SHA1_M0		(5 << 20)
#define  HDCP_DDIE_SHA1_M0		(6 << 20) /* Bspec says 5? */
#define  HDCP_SHA1_BUSY			REG_BIT(16)
#define  HDCP_SHA1_READY		REG_BIT(17)
#define  HDCP_SHA1_COMPLETE		REG_BIT(18)
#define  HDCP_SHA1_V_MATCH		REG_BIT(19)
#define  HDCP_SHA1_TEXT_32		(1 << 1)
#define  HDCP_SHA1_COMPLETE_HASH	(2 << 1)
#define  HDCP_SHA1_TEXT_24		(4 << 1)
#define  HDCP_SHA1_TEXT_16		(5 << 1)
#define  HDCP_SHA1_TEXT_8		(6 << 1)
#define  HDCP_SHA1_TEXT_0		(7 << 1)
#define HDCP_SHA_V_PRIME_H0		_MMIO(0x66d04)
#define HDCP_SHA_V_PRIME_H1		_MMIO(0x66d08)
#define HDCP_SHA_V_PRIME_H2		_MMIO(0x66d0C)
#define HDCP_SHA_V_PRIME_H3		_MMIO(0x66d10)
#define HDCP_SHA_V_PRIME_H4		_MMIO(0x66d14)
#define HDCP_SHA_V_PRIME(h)		_MMIO((0x66d04 + (h) * 4))
#define HDCP_SHA_TEXT			_MMIO(0x66d18)

/* HDCP Auth Registers */
#define _PORTA_HDCP_AUTHENC		0x66800
#define _PORTB_HDCP_AUTHENC		0x66500
#define _PORTC_HDCP_AUTHENC		0x66600
#define _PORTD_HDCP_AUTHENC		0x66700
#define _PORTE_HDCP_AUTHENC		0x66A00
#define _PORTF_HDCP_AUTHENC		0x66900
#define _PORT_HDCP_AUTHENC(port, x)	_MMIO(_PICK(port, \
					  _PORTA_HDCP_AUTHENC, \
					  _PORTB_HDCP_AUTHENC, \
					  _PORTC_HDCP_AUTHENC, \
					  _PORTD_HDCP_AUTHENC, \
					  _PORTE_HDCP_AUTHENC, \
					  _PORTF_HDCP_AUTHENC) + (x))
#define PORT_HDCP_CONF(port)		_PORT_HDCP_AUTHENC(port, 0x0)
#define _TRANSA_HDCP_CONF		0x66400
#define _TRANSB_HDCP_CONF		0x66500
#define TRANS_HDCP_CONF(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP_CONF, \
						    _TRANSB_HDCP_CONF)
#define HDCP_CONF(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_CONF(trans) : \
					 PORT_HDCP_CONF(port))

#define  HDCP_CONF_CAPTURE_AN		REG_BIT(0)
#define  HDCP_CONF_AUTH_AND_ENC		(REG_BIT(1) | REG_BIT(0))
#define PORT_HDCP_ANINIT(port)		_PORT_HDCP_AUTHENC(port, 0x4)
#define _TRANSA_HDCP_ANINIT		0x66404
#define _TRANSB_HDCP_ANINIT		0x66504
#define TRANS_HDCP_ANINIT(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_ANINIT, \
						    _TRANSB_HDCP_ANINIT)
#define HDCP_ANINIT(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_ANINIT(trans) : \
					 PORT_HDCP_ANINIT(port))

#define PORT_HDCP_ANLO(port)		_PORT_HDCP_AUTHENC(port, 0x8)
#define _TRANSA_HDCP_ANLO		0x66408
#define _TRANSB_HDCP_ANLO		0x66508
#define TRANS_HDCP_ANLO(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP_ANLO, \
						    _TRANSB_HDCP_ANLO)
#define HDCP_ANLO(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_ANLO(trans) : \
					 PORT_HDCP_ANLO(port))

#define PORT_HDCP_ANHI(port)		_PORT_HDCP_AUTHENC(port, 0xC)
#define _TRANSA_HDCP_ANHI		0x6640C
#define _TRANSB_HDCP_ANHI		0x6650C
#define TRANS_HDCP_ANHI(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP_ANHI, \
						    _TRANSB_HDCP_ANHI)
#define HDCP_ANHI(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_ANHI(trans) : \
					 PORT_HDCP_ANHI(port))

#define PORT_HDCP_BKSVLO(port)		_PORT_HDCP_AUTHENC(port, 0x10)
#define _TRANSA_HDCP_BKSVLO		0x66410
#define _TRANSB_HDCP_BKSVLO		0x66510
#define TRANS_HDCP_BKSVLO(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_BKSVLO, \
						    _TRANSB_HDCP_BKSVLO)
#define HDCP_BKSVLO(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_BKSVLO(trans) : \
					 PORT_HDCP_BKSVLO(port))

#define PORT_HDCP_BKSVHI(port)		_PORT_HDCP_AUTHENC(port, 0x14)
#define _TRANSA_HDCP_BKSVHI		0x66414
#define _TRANSB_HDCP_BKSVHI		0x66514
#define TRANS_HDCP_BKSVHI(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_BKSVHI, \
						    _TRANSB_HDCP_BKSVHI)
#define HDCP_BKSVHI(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_BKSVHI(trans) : \
					 PORT_HDCP_BKSVHI(port))

#define PORT_HDCP_RPRIME(port)		_PORT_HDCP_AUTHENC(port, 0x18)
#define _TRANSA_HDCP_RPRIME		0x66418
#define _TRANSB_HDCP_RPRIME		0x66518
#define TRANS_HDCP_RPRIME(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_RPRIME, \
						    _TRANSB_HDCP_RPRIME)
#define HDCP_RPRIME(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_RPRIME(trans) : \
					 PORT_HDCP_RPRIME(port))

#define PORT_HDCP_STATUS(port)		_PORT_HDCP_AUTHENC(port, 0x1C)
#define _TRANSA_HDCP_STATUS		0x6641C
#define _TRANSB_HDCP_STATUS		0x6651C
#define TRANS_HDCP_STATUS(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_STATUS, \
						    _TRANSB_HDCP_STATUS)
#define HDCP_STATUS(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_STATUS(trans) : \
					 PORT_HDCP_STATUS(port))

#define  HDCP_STATUS_STREAM_A_ENC	REG_BIT(31)
#define  HDCP_STATUS_STREAM_B_ENC	REG_BIT(30)
#define  HDCP_STATUS_STREAM_C_ENC	REG_BIT(29)
#define  HDCP_STATUS_STREAM_D_ENC	REG_BIT(28)
#define  HDCP_STATUS_AUTH		REG_BIT(21)
#define  HDCP_STATUS_ENC		REG_BIT(20)
#define  HDCP_STATUS_RI_MATCH		REG_BIT(19)
#define  HDCP_STATUS_R0_READY		REG_BIT(18)
#define  HDCP_STATUS_AN_READY		REG_BIT(17)
#define  HDCP_STATUS_CIPHER		REG_BIT(16)
#define  HDCP_STATUS_FRAME_CNT(x)	(((x) >> 8) & 0xff)

/* HDCP2.2 Registers */
#define _PORTA_HDCP2_BASE		0x66800
#define _PORTB_HDCP2_BASE		0x66500
#define _PORTC_HDCP2_BASE		0x66600
#define _PORTD_HDCP2_BASE		0x66700
#define _PORTE_HDCP2_BASE		0x66A00
#define _PORTF_HDCP2_BASE		0x66900
#define _PORT_HDCP2_BASE(port, x)	_MMIO(_PICK((port), \
					  _PORTA_HDCP2_BASE, \
					  _PORTB_HDCP2_BASE, \
					  _PORTC_HDCP2_BASE, \
					  _PORTD_HDCP2_BASE, \
					  _PORTE_HDCP2_BASE, \
					  _PORTF_HDCP2_BASE) + (x))

#define PORT_HDCP2_AUTH(port)		_PORT_HDCP2_BASE(port, 0x98)
#define _TRANSA_HDCP2_AUTH		0x66498
#define _TRANSB_HDCP2_AUTH		0x66598
#define TRANS_HDCP2_AUTH(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP2_AUTH, \
						    _TRANSB_HDCP2_AUTH)
#define   AUTH_LINK_AUTHENTICATED	REG_BIT(31)
#define   AUTH_LINK_TYPE		REG_BIT(30)
#define   AUTH_FORCE_CLR_INPUTCTR	REG_BIT(19)
#define   AUTH_CLR_KEYS			REG_BIT(18)
#define HDCP2_AUTH(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_AUTH(trans) : \
					 PORT_HDCP2_AUTH(port))

#define PORT_HDCP2_CTL(port)		_PORT_HDCP2_BASE(port, 0xB0)
#define _TRANSA_HDCP2_CTL		0x664B0
#define _TRANSB_HDCP2_CTL		0x665B0
#define TRANS_HDCP2_CTL(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP2_CTL, \
						    _TRANSB_HDCP2_CTL)
#define   CTL_LINK_ENCRYPTION_REQ	REG_BIT(31)
#define HDCP2_CTL(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_CTL(trans) : \
					 PORT_HDCP2_CTL(port))

#define PORT_HDCP2_STATUS(port)		_PORT_HDCP2_BASE(port, 0xB4)
#define _TRANSA_HDCP2_STATUS		0x664B4
#define _TRANSB_HDCP2_STATUS		0x665B4
#define TRANS_HDCP2_STATUS(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP2_STATUS, \
						    _TRANSB_HDCP2_STATUS)
#define   LINK_TYPE_STATUS		REG_BIT(22)
#define   LINK_AUTH_STATUS		REG_BIT(21)
#define   LINK_ENCRYPTION_STATUS	REG_BIT(20)
#define HDCP2_STATUS(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_STATUS(trans) : \
					 PORT_HDCP2_STATUS(port))

#define _PIPEA_HDCP2_STREAM_STATUS	0x668C0
#define _PIPEB_HDCP2_STREAM_STATUS	0x665C0
#define _PIPEC_HDCP2_STREAM_STATUS	0x666C0
#define _PIPED_HDCP2_STREAM_STATUS	0x667C0
#define PIPE_HDCP2_STREAM_STATUS(pipe)		_MMIO(_PICK((pipe), \
						      _PIPEA_HDCP2_STREAM_STATUS, \
						      _PIPEB_HDCP2_STREAM_STATUS, \
						      _PIPEC_HDCP2_STREAM_STATUS, \
						      _PIPED_HDCP2_STREAM_STATUS))

#define _TRANSA_HDCP2_STREAM_STATUS		0x664C0
#define _TRANSB_HDCP2_STREAM_STATUS		0x665C0
#define TRANS_HDCP2_STREAM_STATUS(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP2_STREAM_STATUS, \
						    _TRANSB_HDCP2_STREAM_STATUS)
#define   STREAM_ENCRYPTION_STATUS	REG_BIT(31)
#define   STREAM_TYPE_STATUS		REG_BIT(30)
#define HDCP2_STREAM_STATUS(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_STREAM_STATUS(trans) : \
					 PIPE_HDCP2_STREAM_STATUS(pipe))

#define _PORTA_HDCP2_AUTH_STREAM		0x66F00
#define _PORTB_HDCP2_AUTH_STREAM		0x66F04
#define PORT_HDCP2_AUTH_STREAM(port)	_MMIO_PORT(port, \
						   _PORTA_HDCP2_AUTH_STREAM, \
						   _PORTB_HDCP2_AUTH_STREAM)
#define _TRANSA_HDCP2_AUTH_STREAM		0x66F00
#define _TRANSB_HDCP2_AUTH_STREAM		0x66F04
#define TRANS_HDCP2_AUTH_STREAM(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP2_AUTH_STREAM, \
						    _TRANSB_HDCP2_AUTH_STREAM)
#define   AUTH_STREAM_TYPE		REG_BIT(31)
#define HDCP2_AUTH_STREAM(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_AUTH_STREAM(trans) : \
					 PORT_HDCP2_AUTH_STREAM(port))

#endif /* __INTEL_HDCP_REGS_H__ */
