#include "cmp.h"
#include <ieee1394.h>
#include <nodemgr.h>
#include <highlevel.h>
#include <ohci1394.h>
#include <hosts.h>
#include <ieee1394_core.h>
#include <ieee1394_transactions.h>
#include "avc_api.h"

typedef struct _OPCR
{
	BYTE PTPConnCount    : 6 ; // Point to point connect. counter
	BYTE BrConnCount     : 1 ; // Broadcast connection counter
	BYTE OnLine          : 1 ; // On Line

	BYTE ChNr            : 6 ; // Channel number
	BYTE Res             : 2 ; // Reserved

	BYTE PayloadHi       : 2 ; // Payoad high bits
	BYTE OvhdID          : 4 ; // Overhead ID
	BYTE DataRate        : 2 ; // Data Rate

	BYTE PayloadLo           ; // Payoad low byte
} OPCR ;

#define FIRESAT_SPEED IEEE1394_SPEED_400

/* hpsb_lock is being removed from the kernel-source,
 * therefor we define our own 'firesat_hpsb_lock'*/

int send_packet_and_wait(struct hpsb_packet *packet);

int firesat_hpsb_lock(struct hpsb_host *host, nodeid_t node, unsigned int generation,
		u64 addr, int extcode, quadlet_t * data, quadlet_t arg) {

	struct hpsb_packet *packet;
	int retval = 0;

	BUG_ON(in_interrupt()); // We can't be called in an interrupt, yet

	packet = hpsb_make_lockpacket(host, node, addr, extcode, data, arg);
	if (!packet)
		return -ENOMEM;

	packet->generation = generation;
	retval = send_packet_and_wait(packet);
	if (retval < 0)
		goto hpsb_lock_fail;

	retval = hpsb_packet_success(packet);

	if (retval == 0) {
		*data = packet->data[0];
	}

	hpsb_lock_fail:
	hpsb_free_tlabel(packet);
	hpsb_free_packet(packet);

	return retval;
}


static int cmp_read(struct firesat *firesat, void *buffer, u64 addr, size_t length) {
	int ret;
	if(down_interruptible(&firesat->avc_sem))
		return -EINTR;

	ret = hpsb_read(firesat->host, firesat->nodeentry->nodeid, firesat->nodeentry->generation,
		addr, buffer, length);

	up(&firesat->avc_sem);
	return ret;
}

static int cmp_lock(struct firesat *firesat, quadlet_t *data, u64 addr, quadlet_t arg, int ext_tcode) {
	int ret;
	if(down_interruptible(&firesat->avc_sem))
		return -EINTR;

	ret = firesat_hpsb_lock(firesat->host, firesat->nodeentry->nodeid, firesat->nodeentry->generation,
		addr, ext_tcode, data, arg);

	up(&firesat->avc_sem);
	return ret;
}

//try establishing a point-to-point connection (may be interrupted by a busreset
int try_CMPEstablishPPconnection(struct firesat *firesat, int output_plug, int iso_channel) {
	unsigned int BWU; //bandwidth to allocate

	quadlet_t old_oPCR,test_oPCR = 0x0;
	u64 oPCR_address=0xfffff0000904ull+(output_plug << 2);
	int result=cmp_read(firesat, &test_oPCR, oPCR_address, 4);

	printk(KERN_INFO "%s: nodeid = %d\n",__func__,firesat->nodeentry->nodeid);

	if (result < 0) {
		printk("%s: cannot read oPCR\n", __func__);
		return result;
	} else {
		printk(KERN_INFO "%s: oPCR = %08x\n",__func__,test_oPCR);
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
				printk(KERN_INFO "%s: trying compare_swap...\n",__func__);
				printk(KERN_INFO "%s: oPCR_old: %08x, oPCR_new: %08x\n",__func__, old_oPCR, new_oPCR);
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

	printk(KERN_INFO "%s\n",__func__);

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

static void complete_packet(void *data) {
	complete((struct completion *) data);
}

int send_packet_and_wait(struct hpsb_packet *packet) {
	struct completion done;
	int retval;

	init_completion(&done);
	hpsb_set_packet_complete_task(packet, complete_packet, &done);
	retval = hpsb_send_packet(packet);
	if (retval == 0)
		wait_for_completion(&done);

	return retval;
}
