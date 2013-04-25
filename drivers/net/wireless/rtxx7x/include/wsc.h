/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef	__WSC_H__
#define	__WSC_H__

/* WSC OUI SMI */
#define WSC_OUI				0x0050f204
#define	WSC_SMI				0x00372A
#define	WSC_VENDOR_TYPE		0x00000001

/* EAP code */
#define	EAP_CODE_REQ		0x01
#define	EAP_CODE_RSP		0x02
#define	EAP_CODE_FAIL		0x04
#define EAP_TYPE_ID			0x01
#define EAP_TYPE_NOTIFY		0x02
#define	EAP_TYPE_WSC		0xfe

/* structure to store Simple Config Attributes Info */
typedef struct GNU_PACKED _WSC_LV_INFO {
    USHORT  ValueLen;
    UCHAR   Value[512];
} WSC_LV_INFO;

typedef struct GNU_PACKED _WSC_IE_HEADER {
	UCHAR elemId;
	UCHAR length;
	UCHAR oui[4];
} WSC_IE_HEADER;

/* WSC IE structure */
typedef	struct GNU_PACKED	_WSC_IE
{
	USHORT	Type;
	USHORT	Length;
	UCHAR	Data[1];	/* variable length data */
}	WSC_IE, *PWSC_IE;

/* WSC fixed information within EAP */
typedef	struct GNU_PACKED	_WSC_FRAME	{
	UCHAR	SMI[3];
	UINT	VendorType;
	UCHAR	OpCode;
	UCHAR	Flags;
}	WSC_FRAME, *PWSC_FRAME;

/* EAP frame format */
typedef	struct GNU_PACKED	_EAP_FRAME	{
	UCHAR	Code;						/* 1 = Request, 2 = Response */
	UCHAR	Id;
	USHORT	Length;
	UCHAR	Type;						/* 1 = Identity, 0xfe = reserved, used by WSC */
}	EAP_FRAME, *PEAP_FRAME;

static inline BOOLEAN WscCheckWSCHeader(
    IN  PUCHAR              pData)
{
    PWSC_FRAME			pWsc;

	pWsc = (PWSC_FRAME) pData;

    /* Verify SMI first */
	if (((pWsc->SMI[0] * 256 + pWsc->SMI[1]) * 256 + pWsc->SMI[2]) != WSC_SMI)
	{
		/* Wrong WSC SMI Vendor ID, Update WSC status */
		return  FALSE;
	}
    
    /* Verify Vendor Type */
	if (cpu2be32(get_unaligned32(&pWsc->VendorType)) != WSC_VENDOR_TYPE)
	{
		/* Wrong WSC Vendor Type, Update WSC status */
		return  FALSE;
	}
    return TRUE;
}

#endif	/* __WSC_H__ */

