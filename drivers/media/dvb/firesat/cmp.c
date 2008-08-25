/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <hosts.h>
#include <ieee1394.h>
#include <ieee1394_core.h>
#include <ieee1394_transactions.h>
#include <nodemgr.h>

#include "avc_api.h"
#include "cmp.h"
#include "firesat.h"

typedef struct _OPCR
{
	__u8 PTPConnCount    : 6 ; // Point to point connect. counter
	__u8 BrConnCount     : 1 ; // Broadcast connection counter
	__u8 OnLine          : 1 ; // On Line

	__u8 ChNr            : 6 ; // Channel number
	__u8 Res             : 2 ; // Reserved

	__u8 PayloadHi       : 2 ; // Payoad high bits
	__u8 OvhdID          : 4 ; // Overhead ID
	__u8 DataRate        : 2 ; // Data Rate

	__u8 PayloadLo           ; // Payoad low byte
} OPCR ;

#define FIRESAT_SPEED IEEE1394_SPEED_400

static int cmp_read(struct firesat *firesat, void *buf, u64 addr, size_t len)
{
	int ret;

	if (mutex_lock_interruptible(&firesat->avc_mutex))
		return -EINTR;

	ret = hpsb_read(firesat->host, firesat->nodeentry->nodeid,
			firesat->nodeentry->generation, addr, buf, len);

	mutex_unlock(&firesat->avc_mutex);
	return ret;
}

static int cmp_lock(struct firesat *firesat, quadlet_t *data, u64 addr,
		quadlet_t arg, int ext_tcode)
{
	int ret;

	if (mutex_lock_interruptible(&firesat->avc_mutex))
		return -EINTR;

	ret = hpsb_lock(firesat->host, firesat->nodeentry->nodeid,
			firesat->nodeentry->generation,
			addr, ext_tcode, data, arg);

	mutex_unlock(&firesat->avc_mutex);
	return ret;
}

//try establishing a point-to-point connection (may be interrupted by a busreset
int try_CMPEstablishPPconnection(struct firesat *firesat, int output_plug, int iso_channel) {
	unsigned int BWU; //bandwidth to allocate

	quadlet_t old_oPCR,test_oPCR = 0x0;
	u64 oPCR_address=0xfffff0000904ull+(output_plug << 2);
	int result=cmp_read(firesat, &test_oPCR, oPCR_address, 4);

/* 	printk(KERN_INFO "%s: nodeid = %d\n",__func__,firesat->nodeentry->nodeid); */

	if (result < 0) {
		printk("%s: cannot read oPCR\n", __func__);
		return result;
	} else {
/* 		printk(KERN_INFO "%s: oPCR = %08x\n",__func__,test_oPCR); */
		do {
			OPCR *hilf= (OPCR*) &test_oPCR;

			if (!hilf->OnLine) {
				printk("%s: Output offline; oPCR: %08x\n", __func__, test_oPCR);
				return -EBUSY;
			} else {
				quadlet_t new_oPCR;

				old_oPCR=test_oPCR;
				if (hilf->PTPConnCount) {
					if (hilf->ChNr != iso_channel) {
						printk("%s: Output plug has already connection on channel %u; cannot change it to channel %u\n",__func__,hilf->ChNr,iso_channel);
						return -EBUSY;
					} else
						printk(KERN_INFO "%s: Overlaying existing connection; connection counter was: %u\n",__func__, hilf->PTPConnCount);
					BWU=0; //we allocate no bandwidth (is this necessary?)
				} else {
					hilf->ChNr=iso_channel;
					hilf->DataRate=FIRESAT_SPEED;

					hilf->OvhdID=0;      //FIXME: that is for worst case -> optimize
					BWU=hilf->OvhdID?hilf->OvhdID*32:512;
					BWU += (hilf->PayloadLo + (hilf->PayloadHi << 8) +3) * (2 << (3-hilf->DataRate));
/*					if (allocate_1394_resources(iso_channel,BWU))
					{
						cout << "Allocation of resources failed\n";
						return -2;
					}*/
				}

				hilf->PTPConnCount++;
				new_oPCR=test_oPCR;
/* 				printk(KERN_INFO "%s: trying compare_swap...\n",__func__); */
/* 				printk(KERN_INFO "%s: oPCR_old: %08x, oPCR_new: %08x\n",__func__, old_oPCR, new_oPCR); */
				result=cmp_lock(firesat, &test_oPCR, oPCR_address, old_oPCR, 2);

				if (result < 0) {
					printk("%s: cannot compare_swap oPCR\n",__func__);
					return result;
				}
				if ((old_oPCR != test_oPCR) && (!((OPCR*) &old_oPCR)->PTPConnCount))
				{
					printk("%s: change of oPCR failed -> freeing resources\n",__func__);
//					hilf= (OPCR*) &new_oPCR;
//					unsigned int BWU=hilf->OvhdID?hilf->OvhdID*32:512;
//					BWU += (hilf->Payload+3) * (2 << (3-hilf->DataRate));
/*					if (deallocate_1394_resources(iso_channel,BWU))
					{

						cout << "Deallocation of resources failed\n";
						return -3;
					}*/
				}
			}
		}
		while (old_oPCR != test_oPCR);
	}
	return 0;
}

//try breaking a point-to-point connection (may be interrupted by a busreset
int try_CMPBreakPPconnection(struct firesat *firesat, int output_plug,int iso_channel) {
	quadlet_t old_oPCR,test_oPCR;

	u64 oPCR_address=0xfffff0000904ull+(output_plug << 2);
	int result=cmp_read(firesat, &test_oPCR, oPCR_address, 4);

/* 	printk(KERN_INFO "%s\n",__func__); */

	if (result < 0) {
		printk("%s: cannot read oPCR\n", __func__);
		return result;
	} else {
		do {
			OPCR *hilf= (OPCR*) &test_oPCR;

			if (!hilf->OnLine || !hilf->PTPConnCount || hilf->ChNr != iso_channel) {
				printk("%s: Output plug does not have PtP-connection on that channel; oPCR: %08x\n", __func__, test_oPCR);
				return -EINVAL;
			} else {
				quadlet_t new_oPCR;
				old_oPCR=test_oPCR;
				hilf->PTPConnCount--;
				new_oPCR=test_oPCR;

//				printk(KERN_INFO "%s: trying compare_swap...\n", __func__);
				result=cmp_lock(firesat, &test_oPCR, oPCR_address, old_oPCR, 2);
				if (result < 0) {
					printk("%s: cannot compare_swap oPCR\n",__func__);
					return result;
				}
			}

		} while (old_oPCR != test_oPCR);

/*		hilf = (OPCR*) &old_oPCR;
		if (hilf->PTPConnCount == 1) { // if we were the last owner of this connection
			cout << "deallocating 1394 resources\n";
			unsigned int BWU=hilf->OvhdID?hilf->OvhdID*32:512;
			BWU += (hilf->PayloadLo + (hilf->PayloadHi << 8) +3) * (2 << (3-hilf->DataRate));
			if (deallocate_1394_resources(iso_channel,BWU))
			{
				cout << "Deallocation of resources failed\n";
				return -3;
			}
		}*/
    }
	return 0;
}
