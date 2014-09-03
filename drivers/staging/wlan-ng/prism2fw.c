/* from src/prism2/download/prism2dl.c
*
* utility for downloading prism2 images moved into kernelspace
*
* Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
* --------------------------------------------------------------------
*
* linux-wlan
*
*   The contents of this file are subject to the Mozilla Public
*   License Version 1.1 (the "License"); you may not use this file
*   except in compliance with the License. You may obtain a copy of
*   the License at http://www.mozilla.org/MPL/
*
*   Software distributed under the License is distributed on an "AS
*   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
*   implied. See the License for the specific language governing
*   rights and limitations under the License.
*
*   Alternatively, the contents of this file may be used under the
*   terms of the GNU Public License version 2 (the "GPL"), in which
*   case the provisions of the GPL are applicable instead of the
*   above.  If you wish to allow the use of your version of this file
*   only under the terms of the GPL and not to allow others to use
*   your version of this file under the MPL, indicate your decision
*   by deleting the provisions above and replace them with the notice
*   and other provisions required by the GPL.  If you do not delete
*   the provisions above, a recipient may use your version of this
*   file under either the MPL or the GPL.
*
* --------------------------------------------------------------------
*
* Inquiries regarding the linux-wlan Open Source project can be
* made directly to:
*
* AbsoluteValue Systems Inc.
* info@linux-wlan.com
* http://www.linux-wlan.com
*
* --------------------------------------------------------------------
*
* Portions of the development of this software were funded by
* Intersil Corporation as part of PRISM(R) chipset product development.
*
* --------------------------------------------------------------------
*/

/*================================================================*/
/* System Includes */
#include <linux/ihex.h>
#include <linux/slab.h>

/*================================================================*/
/* Local Constants */

#define PRISM2_USB_FWFILE	"prism2_ru.fw"
MODULE_FIRMWARE(PRISM2_USB_FWFILE);

#define S3DATA_MAX		5000
#define S3PLUG_MAX		200
#define S3CRC_MAX		200
#define S3INFO_MAX		50

#define S3ADDR_PLUG		(0xff000000UL)
#define S3ADDR_CRC		(0xff100000UL)
#define S3ADDR_INFO		(0xff200000UL)
#define S3ADDR_START		(0xff400000UL)

#define CHUNKS_MAX		100

#define WRITESIZE_MAX		4096

/*================================================================*/
/* Local Types */

struct s3datarec {
	u32 len;
	u32 addr;
	u8 checksum;
	u8 *data;
};

struct s3plugrec {
	u32 itemcode;
	u32 addr;
	u32 len;
};

struct s3crcrec {
	u32 addr;
	u32 len;
	unsigned int dowrite;
};

struct s3inforec {
	u16 len;
	u16 type;
	union {
		hfa384x_compident_t version;
		hfa384x_caplevel_t compat;
		u16 buildseq;
		hfa384x_compident_t platform;
	} info;
};

struct pda {
	u8 buf[HFA384x_PDA_LEN_MAX];
	hfa384x_pdrec_t *rec[HFA384x_PDA_RECS_MAX];
	unsigned int nrec;
};

struct imgchunk {
	u32 addr;	/* start address */
	u32 len;	/* in bytes */
	u16 crc;	/* CRC value (if it falls at a chunk boundary) */
	u8 *data;
};

/*================================================================*/
/* Local Static Definitions */

/*----------------------------------------------------------------*/
/* s-record image processing */

/* Data records */
static unsigned int ns3data;
static struct s3datarec s3data[S3DATA_MAX];

/* Plug records */
static unsigned int ns3plug;
static struct s3plugrec s3plug[S3PLUG_MAX];

/* CRC records */
static unsigned int ns3crc;
static struct s3crcrec s3crc[S3CRC_MAX];

/* Info records */
static unsigned int ns3info;
static struct s3inforec s3info[S3INFO_MAX];

/* S7 record (there _better_ be only one) */
static u32 startaddr;

/* Load image chunks */
static unsigned int nfchunks;
static struct imgchunk fchunk[CHUNKS_MAX];

/* Note that for the following pdrec_t arrays, the len and code */
/*   fields are stored in HOST byte order. The mkpdrlist() function */
/*   does the conversion.  */
/*----------------------------------------------------------------*/
/* PDA, built from [card|newfile]+[addfile1+addfile2...] */

static struct pda pda;
static hfa384x_compident_t nicid;
static hfa384x_caplevel_t rfid;
static hfa384x_caplevel_t macid;
static hfa384x_caplevel_t priid;

/*================================================================*/
/* Local Function Declarations */

static int prism2_fwapply(const struct ihex_binrec *rfptr,
wlandevice_t *wlandev);

static int read_fwfile(const struct ihex_binrec *rfptr);

static int mkimage(struct imgchunk *clist, unsigned int *ccnt);

static int read_cardpda(struct pda *pda, wlandevice_t *wlandev);

static int mkpdrlist(struct pda *pda);

static int plugimage(struct imgchunk *fchunk, unsigned int nfchunks,
	      struct s3plugrec *s3plug, unsigned int ns3plug, struct pda *pda);

static int crcimage(struct imgchunk *fchunk, unsigned int nfchunks,
	     struct s3crcrec *s3crc, unsigned int ns3crc);

static int writeimage(wlandevice_t *wlandev, struct imgchunk *fchunk,
	       unsigned int nfchunks);
static void free_chunks(struct imgchunk *fchunk, unsigned int *nfchunks);

static void free_srecs(void);

static int validate_identity(void);

/*================================================================*/
/* Function Definitions */

/*----------------------------------------------------------------
* prism2_fwtry
*
* Try and get firmware into memory
*
* Arguments:
*	udev	usb device structure
*	wlandev wlan device structure
*
* Returns:
*	0	- success
*	~0	- failure
----------------------------------------------------------------*/
static int prism2_fwtry(struct usb_device *udev, wlandevice_t *wlandev)
{
	const struct firmware *fw_entry = NULL;

	netdev_info(wlandev->netdev, "prism2_usb: Checking for firmware %s\n",
	       PRISM2_USB_FWFILE);
	if (request_ihex_firmware(&fw_entry,
				  PRISM2_USB_FWFILE, &udev->dev) != 0) {
		netdev_info(wlandev->netdev,
		       "prism2_usb: Firmware not available, but not essential\n");
		netdev_info(wlandev->netdev,
		       "prism2_usb: can continue to use card anyway.\n");
		return 1;
	}

	netdev_info(wlandev->netdev,
		    "prism2_usb: %s will be processed, size %zu\n",
		    PRISM2_USB_FWFILE, fw_entry->size);
	prism2_fwapply((const struct ihex_binrec *)fw_entry->data, wlandev);

	release_firmware(fw_entry);
	return 0;
}

/*----------------------------------------------------------------
* prism2_fwapply
*
* Apply the firmware loaded into memory
*
* Arguments:
*	rfptr	firmware image in kernel memory
*	wlandev device
*
* Returns:
*	0	- success
*	~0	- failure
----------------------------------------------------------------*/
static int prism2_fwapply(const struct ihex_binrec *rfptr, wlandevice_t *wlandev)
{
	signed int result = 0;
	struct p80211msg_dot11req_mibget getmsg;
	p80211itemd_t *item;
	u32 *data;

	/* Initialize the data structures */
	ns3data = 0;
	memset(s3data, 0, sizeof(s3data));
	ns3plug = 0;
	memset(s3plug, 0, sizeof(s3plug));
	ns3crc = 0;
	memset(s3crc, 0, sizeof(s3crc));
	ns3info = 0;
	memset(s3info, 0, sizeof(s3info));
	startaddr = 0;

	nfchunks = 0;
	memset(fchunk, 0, sizeof(fchunk));
	memset(&nicid, 0, sizeof(nicid));
	memset(&rfid, 0, sizeof(rfid));
	memset(&macid, 0, sizeof(macid));
	memset(&priid, 0, sizeof(priid));

	/* clear the pda and add an initial END record */
	memset(&pda, 0, sizeof(pda));
	pda.rec[0] = (hfa384x_pdrec_t *) pda.buf;
	pda.rec[0]->len = cpu_to_le16(2);	/* len in words */
	pda.rec[0]->code = cpu_to_le16(HFA384x_PDR_END_OF_PDA);
	pda.nrec = 1;

	/*-----------------------------------------------------*/
	/* Put card into fwload state */
	prism2sta_ifstate(wlandev, P80211ENUM_ifstate_fwload);

	/* Build the PDA we're going to use. */
	if (read_cardpda(&pda, wlandev)) {
		netdev_err(wlandev->netdev, "load_cardpda failed, exiting.\n");
		return 1;
	}

	/* read the card's PRI-SUP */
	memset(&getmsg, 0, sizeof(getmsg));
	getmsg.msgcode = DIDmsg_dot11req_mibget;
	getmsg.msglen = sizeof(getmsg);
	strcpy(getmsg.devname, wlandev->name);

	getmsg.mibattribute.did = DIDmsg_dot11req_mibget_mibattribute;
	getmsg.mibattribute.status = P80211ENUM_msgitem_status_data_ok;
	getmsg.resultcode.did = DIDmsg_dot11req_mibget_resultcode;
	getmsg.resultcode.status = P80211ENUM_msgitem_status_no_value;

	item = (p80211itemd_t *) getmsg.mibattribute.data;
	item->did = DIDmib_p2_p2NIC_p2PRISupRange;
	item->status = P80211ENUM_msgitem_status_no_value;

	data = (u32 *) item->data;

	/* DIDmsg_dot11req_mibget */
	prism2mgmt_mibset_mibget(wlandev, &getmsg);
	if (getmsg.resultcode.data != P80211ENUM_resultcode_success)
		netdev_err(wlandev->netdev, "Couldn't fetch PRI-SUP info\n");

	/* Already in host order */
	priid.role = *data++;
	priid.id = *data++;
	priid.variant = *data++;
	priid.bottom = *data++;
	priid.top = *data++;

	/* Read the S3 file */
	result = read_fwfile(rfptr);
	if (result) {
		netdev_err(wlandev->netdev,
			   "Failed to read the data exiting.\n");
		return 1;
	}

	result = validate_identity();

	if (result) {
		netdev_err(wlandev->netdev, "Incompatible firmware image.\n");
		return 1;
	}

	if (startaddr == 0x00000000) {
		netdev_err(wlandev->netdev,
			   "Can't RAM download a Flash image!\n");
		return 1;
	}

	/* Make the image chunks */
	result = mkimage(fchunk, &nfchunks);

	/* Do any plugging */
	result = plugimage(fchunk, nfchunks, s3plug, ns3plug, &pda);
	if (result) {
		netdev_err(wlandev->netdev, "Failed to plug data.\n");
		return 1;
	}

	/* Insert any CRCs */
	if (crcimage(fchunk, nfchunks, s3crc, ns3crc)) {
		netdev_err(wlandev->netdev, "Failed to insert all CRCs\n");
		return 1;
	}

	/* Write the image */
	result = writeimage(wlandev, fchunk, nfchunks);
	if (result) {
		netdev_err(wlandev->netdev, "Failed to ramwrite image data.\n");
		return 1;
	}

	/* clear any allocated memory */
	free_chunks(fchunk, &nfchunks);
	free_srecs();

	netdev_info(wlandev->netdev, "prism2_usb: firmware loading finished.\n");

	return result;
}

/*----------------------------------------------------------------
* crcimage
*
* Adds a CRC16 in the two bytes prior to each block identified by
* an S3 CRC record.  Currently, we don't actually do a CRC we just
* insert the value 0xC0DE in hfa384x order.
*
* Arguments:
*	fchunk		Array of image chunks
*	nfchunks	Number of image chunks
*	s3crc		Array of crc records
*	ns3crc		Number of crc records
*
* Returns:
*	0	success
*	~0	failure
----------------------------------------------------------------*/
static int crcimage(struct imgchunk *fchunk, unsigned int nfchunks,
	     struct s3crcrec *s3crc, unsigned int ns3crc)
{
	int result = 0;
	int i;
	int c;
	u32 crcstart;
	u32 crcend;
	u32 cstart = 0;
	u32 cend;
	u8 *dest;
	u32 chunkoff;

	for (i = 0; i < ns3crc; i++) {
		if (!s3crc[i].dowrite)
			continue;
		crcstart = s3crc[i].addr;
		crcend = s3crc[i].addr + s3crc[i].len;
		/* Find chunk */
		for (c = 0; c < nfchunks; c++) {
			cstart = fchunk[c].addr;
			cend = fchunk[c].addr + fchunk[c].len;
			/* the line below does an address & len match search */
			/* unfortunately, I've found that the len fields of */
			/* some crc records don't match with the length of */
			/* the actual data, so we're not checking right now */
			/* if (crcstart-2 >= cstart && crcend <= cend) break; */

			/* note the -2 below, it's to make sure the chunk has */
			/* space for the CRC value */
			if (crcstart - 2 >= cstart && crcstart < cend)
				break;
		}
		if (c >= nfchunks) {
			pr_err("Failed to find chunk for "
			       "crcrec[%d], addr=0x%06x len=%d , "
			       "aborting crc.\n",
			       i, s3crc[i].addr, s3crc[i].len);
			return 1;
		}

		/* Insert crc */
		pr_debug("Adding crc @ 0x%06x\n", s3crc[i].addr - 2);
		chunkoff = crcstart - cstart - 2;
		dest = fchunk[c].data + chunkoff;
		*dest = 0xde;
		*(dest + 1) = 0xc0;

	}
	return result;
}

/*----------------------------------------------------------------
* free_chunks
*
* Clears the chunklist data structures in preparation for a new file.
*
* Arguments:
*	none
*
* Returns:
*	nothing
----------------------------------------------------------------*/
static void free_chunks(struct imgchunk *fchunk, unsigned int *nfchunks)
{
	int i;

	for (i = 0; i < *nfchunks; i++)
		kfree(fchunk[i].data);

	*nfchunks = 0;
	memset(fchunk, 0, sizeof(*fchunk));

}

/*----------------------------------------------------------------
* free_srecs
*
* Clears the srec data structures in preparation for a new file.
*
* Arguments:
*	none
*
* Returns:
*	nothing
----------------------------------------------------------------*/
static void free_srecs(void)
{
	ns3data = 0;
	memset(s3data, 0, sizeof(s3data));
	ns3plug = 0;
	memset(s3plug, 0, sizeof(s3plug));
	ns3crc = 0;
	memset(s3crc, 0, sizeof(s3crc));
	ns3info = 0;
	memset(s3info, 0, sizeof(s3info));
	startaddr = 0;
}

/*----------------------------------------------------------------
* mkimage
*
* Scans the currently loaded set of S records for data residing
* in contiguous memory regions.  Each contiguous region is then
* made into a 'chunk'.  This function assumes that we're building
* a new chunk list.  Assumes the s3data items are in sorted order.
*
* Arguments:	none
*
* Returns:
*	0	- success
*	~0	- failure (probably an errno)
----------------------------------------------------------------*/
static int mkimage(struct imgchunk *clist, unsigned int *ccnt)
{
	int result = 0;
	int i;
	int j;
	int currchunk = 0;
	u32 nextaddr = 0;
	u32 s3start;
	u32 s3end;
	u32 cstart = 0;
	u32 cend;
	u32 coffset;

	/* There may already be data in the chunklist */
	*ccnt = 0;

	/* Establish the location and size of each chunk */
	for (i = 0; i < ns3data; i++) {
		if (s3data[i].addr == nextaddr) {
			/* existing chunk, grow it */
			clist[currchunk].len += s3data[i].len;
			nextaddr += s3data[i].len;
		} else {
			/* New chunk */
			(*ccnt)++;
			currchunk = *ccnt - 1;
			clist[currchunk].addr = s3data[i].addr;
			clist[currchunk].len = s3data[i].len;
			nextaddr = s3data[i].addr + s3data[i].len;
			/* Expand the chunk if there is a CRC record at */
			/* their beginning bound */
			for (j = 0; j < ns3crc; j++) {
				if (s3crc[j].dowrite &&
				    s3crc[j].addr == clist[currchunk].addr) {
					clist[currchunk].addr -= 2;
					clist[currchunk].len += 2;
				}
			}
		}
	}

	/* We're currently assuming there aren't any overlapping chunks */
	/*  if this proves false, we'll need to add code to coalesce. */

	/* Allocate buffer space for chunks */
	for (i = 0; i < *ccnt; i++) {
		clist[i].data = kzalloc(clist[i].len, GFP_KERNEL);
		if (clist[i].data == NULL) {
			pr_err("failed to allocate image space, exitting.\n");
			return 1;
		}
		pr_debug("chunk[%d]: addr=0x%06x len=%d\n",
			 i, clist[i].addr, clist[i].len);
	}

	/* Copy srec data to chunks */
	for (i = 0; i < ns3data; i++) {
		s3start = s3data[i].addr;
		s3end = s3start + s3data[i].len - 1;
		for (j = 0; j < *ccnt; j++) {
			cstart = clist[j].addr;
			cend = cstart + clist[j].len - 1;
			if (s3start >= cstart && s3end <= cend)
				break;
		}
		if (((unsigned int)j) >= (*ccnt)) {
			pr_err("s3rec(a=0x%06x,l=%d), no chunk match, exiting.\n",
			       s3start, s3data[i].len);
			return 1;
		}
		coffset = s3start - cstart;
		memcpy(clist[j].data + coffset, s3data[i].data, s3data[i].len);
	}

	return result;
}

/*----------------------------------------------------------------
* mkpdrlist
*
* Reads a raw PDA and builds an array of pdrec_t structures.
*
* Arguments:
*	pda	buffer containing raw PDA bytes
*	pdrec	ptr to an array of pdrec_t's.  Will be filled on exit.
*	nrec	ptr to a variable that will contain the count of PDRs
*
* Returns:
*	0	- success
*	~0	- failure (probably an errno)
----------------------------------------------------------------*/
static int mkpdrlist(struct pda *pda)
{
	int result = 0;
	u16 *pda16 = (u16 *) pda->buf;
	int curroff;		/* in 'words' */

	pda->nrec = 0;
	curroff = 0;
	while (curroff < (HFA384x_PDA_LEN_MAX / 2) &&
	       le16_to_cpu(pda16[curroff + 1]) != HFA384x_PDR_END_OF_PDA) {
		pda->rec[pda->nrec] = (hfa384x_pdrec_t *) &(pda16[curroff]);

		if (le16_to_cpu(pda->rec[pda->nrec]->code) ==
		    HFA384x_PDR_NICID) {
			memcpy(&nicid, &pda->rec[pda->nrec]->data.nicid,
			       sizeof(nicid));
			nicid.id = le16_to_cpu(nicid.id);
			nicid.variant = le16_to_cpu(nicid.variant);
			nicid.major = le16_to_cpu(nicid.major);
			nicid.minor = le16_to_cpu(nicid.minor);
		}
		if (le16_to_cpu(pda->rec[pda->nrec]->code) ==
		    HFA384x_PDR_MFISUPRANGE) {
			memcpy(&rfid, &pda->rec[pda->nrec]->data.mfisuprange,
			       sizeof(rfid));
			rfid.id = le16_to_cpu(rfid.id);
			rfid.variant = le16_to_cpu(rfid.variant);
			rfid.bottom = le16_to_cpu(rfid.bottom);
			rfid.top = le16_to_cpu(rfid.top);
		}
		if (le16_to_cpu(pda->rec[pda->nrec]->code) ==
		    HFA384x_PDR_CFISUPRANGE) {
			memcpy(&macid, &pda->rec[pda->nrec]->data.cfisuprange,
			       sizeof(macid));
			macid.id = le16_to_cpu(macid.id);
			macid.variant = le16_to_cpu(macid.variant);
			macid.bottom = le16_to_cpu(macid.bottom);
			macid.top = le16_to_cpu(macid.top);
		}

		(pda->nrec)++;
		curroff += le16_to_cpu(pda16[curroff]) + 1;

	}
	if (curroff >= (HFA384x_PDA_LEN_MAX / 2)) {
		pr_err("no end record found or invalid lengths in "
		       "PDR data, exiting. %x %d\n", curroff, pda->nrec);
		return 1;
	}
	if (le16_to_cpu(pda16[curroff + 1]) == HFA384x_PDR_END_OF_PDA) {
		pda->rec[pda->nrec] = (hfa384x_pdrec_t *) &(pda16[curroff]);
		(pda->nrec)++;
	}
	return result;
}

/*----------------------------------------------------------------
* plugimage
*
* Plugs the given image using the given plug records from the given
* PDA and filename.
*
* Arguments:
*	fchunk		Array of image chunks
*	nfchunks	Number of image chunks
*	s3plug		Array of plug records
*	ns3plug		Number of plug records
*	pda		Current pda data
*
* Returns:
*	0	success
*	~0	failure
----------------------------------------------------------------*/
static int plugimage(struct imgchunk *fchunk, unsigned int nfchunks,
	      struct s3plugrec *s3plug, unsigned int ns3plug, struct pda *pda)
{
	int result = 0;
	int i;			/* plug index */
	int j;			/* index of PDR or -1 if fname plug */
	int c;			/* chunk index */
	u32 pstart;
	u32 pend;
	u32 cstart = 0;
	u32 cend;
	u32 chunkoff;
	u8 *dest;

	/* for each plug record */
	for (i = 0; i < ns3plug; i++) {
		pstart = s3plug[i].addr;
		pend = s3plug[i].addr + s3plug[i].len;
		/* find the matching PDR (or filename) */
		if (s3plug[i].itemcode != 0xffffffffUL) { /* not filename */
			for (j = 0; j < pda->nrec; j++) {
				if (s3plug[i].itemcode ==
				    le16_to_cpu(pda->rec[j]->code))
					break;
			}
		} else {
			j = -1;
		}
		if (j >= pda->nrec && j != -1) { /*  if no matching PDR, fail */
			pr_warn("warning: Failed to find PDR for "
			       "plugrec 0x%04x.\n", s3plug[i].itemcode);
			continue;	/* and move on to the next PDR */
#if 0
			/* MSM: They swear that unless it's the MAC address,
			 * the serial number, or the TX calibration records,
			 * then there's reasonable defaults in the f/w
			 * image.  Therefore, missing PDRs in the card
			 * should only be a warning, not fatal.
			 * TODO: add fatals for the PDRs mentioned above.
			 */
			result = 1;
			continue;
#endif
		}

		/* Validate plug len against PDR len */
		if (j != -1 && s3plug[i].len < le16_to_cpu(pda->rec[j]->len)) {
			pr_err("error: Plug vs. PDR len mismatch for "
			       "plugrec 0x%04x, abort plugging.\n",
			       s3plug[i].itemcode);
			result = 1;
			continue;
		}

		/* Validate plug address against chunk data and identify chunk */
		for (c = 0; c < nfchunks; c++) {
			cstart = fchunk[c].addr;
			cend = fchunk[c].addr + fchunk[c].len;
			if (pstart >= cstart && pend <= cend)
				break;
		}
		if (c >= nfchunks) {
			pr_err("error: Failed to find image chunk for "
			       "plugrec 0x%04x.\n", s3plug[i].itemcode);
			result = 1;
			continue;
		}

		/* Plug data */
		chunkoff = pstart - cstart;
		dest = fchunk[c].data + chunkoff;
		pr_debug("Plugging item 0x%04x @ 0x%06x, len=%d, "
			 "cnum=%d coff=0x%06x\n",
			 s3plug[i].itemcode, pstart, s3plug[i].len,
			 c, chunkoff);

		if (j == -1) {	/* plug the filename */
			memset(dest, 0, s3plug[i].len);
			strncpy(dest, PRISM2_USB_FWFILE, s3plug[i].len - 1);
		} else {	/* plug a PDR */
			memcpy(dest, &(pda->rec[j]->data), s3plug[i].len);
		}
	}
	return result;

}

/*----------------------------------------------------------------
* read_cardpda
*
* Sends the command for the driver to read the pda from the card
* named in the device variable.  Upon success, the card pda is
* stored in the "cardpda" variables.  Note that the pda structure
* is considered 'well formed' after this function.  That means
* that the nrecs is valid, the rec array has been set up, and there's
* a valid PDAEND record in the raw PDA data.
*
* Arguments:
*	pda		pda structure
*	wlandev		device
*
* Returns:
*	0	- success
*	~0	- failure (probably an errno)
----------------------------------------------------------------*/
static int read_cardpda(struct pda *pda, wlandevice_t *wlandev)
{
	int result = 0;
	struct p80211msg_p2req_readpda msg;

	/* set up the msg */
	msg.msgcode = DIDmsg_p2req_readpda;
	msg.msglen = sizeof(msg);
	strcpy(msg.devname, wlandev->name);
	msg.pda.did = DIDmsg_p2req_readpda_pda;
	msg.pda.len = HFA384x_PDA_LEN_MAX;
	msg.pda.status = P80211ENUM_msgitem_status_no_value;
	msg.resultcode.did = DIDmsg_p2req_readpda_resultcode;
	msg.resultcode.len = sizeof(u32);
	msg.resultcode.status = P80211ENUM_msgitem_status_no_value;

	if (prism2mgmt_readpda(wlandev, &msg) != 0) {
		/* prism2mgmt_readpda prints an errno if appropriate */
		result = -1;
	} else if (msg.resultcode.data == P80211ENUM_resultcode_success) {
		memcpy(pda->buf, msg.pda.data, HFA384x_PDA_LEN_MAX);
		result = mkpdrlist(pda);
	} else {
		/* resultcode must've been something other than success */
		result = -1;
	}

	return result;
}

/*----------------------------------------------------------------
* read_fwfile
*
* Reads the given fw file which should have been compiled from an srec
* file. Each record in the fw file will either be a plain data record,
* a start address record, or other records used for plugging.
*
* Note that data records are expected to be sorted into
* ascending address order in the fw file.
*
* Note also that the start address record, originally an S7 record in
* the srec file, is expected in the fw file to be like a data record but
* with a certain address to make it identifiable.
*
* Here's the SREC format that the fw should have come from:
* S[37]nnaaaaaaaaddd...dddcc
*
*       nn - number of bytes starting with the address field
* aaaaaaaa - address in readable (or big endian) format
* dd....dd - 0-245 data bytes (two chars per byte)
*       cc - checksum
*
* The S7 record's (there should be only one) address value gets
* converted to an S3 record with address of 0xff400000, with the
* start address being stored as a 4 byte data word. That address is
* the start execution address used for RAM downloads.
*
* The S3 records have a collection of subformats indicated by the
* value of aaaaaaaa:
*   0xff000000 - Plug record, data field format:
*                xxxxxxxxaaaaaaaassssssss
*                x - PDR code number (little endian)
*                a - Address in load image to plug (little endian)
*                s - Length of plug data area (little endian)
*
*   0xff100000 - CRC16 generation record, data field format:
*                aaaaaaaassssssssbbbbbbbb
*                a - Start address for CRC calculation (little endian)
*                s - Length of data to  calculate over (little endian)
*                b - Boolean, true=write crc, false=don't write
*
*   0xff200000 - Info record, data field format:
*                ssssttttdd..dd
*                s - Size in words (little endian)
*                t - Info type (little endian), see #defines and
*                    struct s3inforec for details about types.
*                d - (s - 1) little endian words giving the contents of
*                    the given info type.
*
*   0xff400000 - Start address record, data field format:
*                aaaaaaaa
*                a - Address in load image to plug (little endian)
*
* Arguments:
*	record	firmware image (ihex record structure) in kernel memory
*
* Returns:
*	0	- success
*	~0	- failure (probably an errno)
----------------------------------------------------------------*/
static int read_fwfile(const struct ihex_binrec *record)
{
	int		i;
	int		rcnt = 0;
	u16		*tmpinfo;
	u16		*ptr16;
	u32		*ptr32, len, addr;

	pr_debug("Reading fw file ...\n");

	while (record) {

		rcnt++;

		len = be16_to_cpu(record->len);
		addr = be32_to_cpu(record->addr);

		/* Point into data for different word lengths */
		ptr32 = (u32 *) record->data;
		ptr16 = (u16 *) record->data;

		/* parse what was an S3 srec and put it in the right array */
		switch (addr) {
		case S3ADDR_START:
			startaddr = *ptr32;
			pr_debug("  S7 start addr, record=%d "
				      " addr=0x%08x\n",
				      rcnt,
				      startaddr);
			break;
		case S3ADDR_PLUG:
			s3plug[ns3plug].itemcode = *ptr32;
			s3plug[ns3plug].addr = *(ptr32 + 1);
			s3plug[ns3plug].len = *(ptr32 + 2);

			pr_debug("  S3 plugrec, record=%d "
				      "itemcode=0x%08x addr=0x%08x len=%d\n",
				      rcnt,
				      s3plug[ns3plug].itemcode,
				      s3plug[ns3plug].addr,
				      s3plug[ns3plug].len);

			ns3plug++;
			if (ns3plug == S3PLUG_MAX) {
				pr_err("S3 plugrec limit reached - aborting\n");
				return 1;
			}
			break;
		case S3ADDR_CRC:
			s3crc[ns3crc].addr = *ptr32;
			s3crc[ns3crc].len = *(ptr32 + 1);
			s3crc[ns3crc].dowrite = *(ptr32 + 2);

			pr_debug("  S3 crcrec, record=%d "
				      "addr=0x%08x len=%d write=0x%08x\n",
				      rcnt,
				      s3crc[ns3crc].addr,
				      s3crc[ns3crc].len,
				      s3crc[ns3crc].dowrite);
			ns3crc++;
			if (ns3crc == S3CRC_MAX) {
				pr_err("S3 crcrec limit reached - aborting\n");
				return 1;
			}
			break;
		case S3ADDR_INFO:
			s3info[ns3info].len = *ptr16;
			s3info[ns3info].type = *(ptr16 + 1);

			pr_debug("  S3 inforec, record=%d "
			      "len=0x%04x type=0x%04x\n",
				      rcnt,
				      s3info[ns3info].len,
				      s3info[ns3info].type);
			if (((s3info[ns3info].len - 1) * sizeof(u16)) > sizeof(s3info[ns3info].info)) {
				pr_err("S3 inforec length too long - aborting\n");
				return 1;
			}

			tmpinfo = (u16 *)&(s3info[ns3info].info.version);
			pr_debug("            info=");
			for (i = 0; i < s3info[ns3info].len - 1; i++) {
				tmpinfo[i] = *(ptr16 + 2 + i);
				pr_debug("%04x ", tmpinfo[i]);
			}
			pr_debug("\n");

			ns3info++;
			if (ns3info == S3INFO_MAX) {
				pr_err("S3 inforec limit reached - aborting\n");
				return 1;
			}
			break;
		default:	/* Data record */
			s3data[ns3data].addr = addr;
			s3data[ns3data].len = len;
			s3data[ns3data].data = (uint8_t *) record->data;
			ns3data++;
			if (ns3data == S3DATA_MAX) {
				pr_err("S3 datarec limit reached - aborting\n");
				return 1;
			}
			break;
		}
		record = ihex_next_binrec(record);
	}
	return 0;
}

/*----------------------------------------------------------------
* writeimage
*
* Takes the chunks, builds p80211 messages and sends them down
* to the driver for writing to the card.
*
* Arguments:
*	wlandev		device
*	fchunk		Array of image chunks
*	nfchunks	Number of image chunks
*
* Returns:
*	0	success
*	~0	failure
----------------------------------------------------------------*/
static int writeimage(wlandevice_t *wlandev, struct imgchunk *fchunk,
	       unsigned int nfchunks)
{
	int result = 0;
	struct p80211msg_p2req_ramdl_state *rstmsg;
	struct p80211msg_p2req_ramdl_write *rwrmsg;
	u32 resultcode;
	int i;
	int j;
	unsigned int nwrites;
	u32 curroff;
	u32 currlen;
	u32 currdaddr;

	rstmsg = kmalloc(sizeof(*rstmsg), GFP_KERNEL);
	rwrmsg = kmalloc(sizeof(*rwrmsg), GFP_KERNEL);
	if (!rstmsg || !rwrmsg) {
		kfree(rstmsg);
		kfree(rwrmsg);
		netdev_err(wlandev->netdev,
			   "writeimage: no memory for firmware download, "
			   "aborting download\n");
		return -ENOMEM;
	}

	/* Initialize the messages */
	memset(rstmsg, 0, sizeof(*rstmsg));
	strcpy(rstmsg->devname, wlandev->name);
	rstmsg->msgcode = DIDmsg_p2req_ramdl_state;
	rstmsg->msglen = sizeof(*rstmsg);
	rstmsg->enable.did = DIDmsg_p2req_ramdl_state_enable;
	rstmsg->exeaddr.did = DIDmsg_p2req_ramdl_state_exeaddr;
	rstmsg->resultcode.did = DIDmsg_p2req_ramdl_state_resultcode;
	rstmsg->enable.status = P80211ENUM_msgitem_status_data_ok;
	rstmsg->exeaddr.status = P80211ENUM_msgitem_status_data_ok;
	rstmsg->resultcode.status = P80211ENUM_msgitem_status_no_value;
	rstmsg->enable.len = sizeof(u32);
	rstmsg->exeaddr.len = sizeof(u32);
	rstmsg->resultcode.len = sizeof(u32);

	memset(rwrmsg, 0, sizeof(*rwrmsg));
	strcpy(rwrmsg->devname, wlandev->name);
	rwrmsg->msgcode = DIDmsg_p2req_ramdl_write;
	rwrmsg->msglen = sizeof(*rwrmsg);
	rwrmsg->addr.did = DIDmsg_p2req_ramdl_write_addr;
	rwrmsg->len.did = DIDmsg_p2req_ramdl_write_len;
	rwrmsg->data.did = DIDmsg_p2req_ramdl_write_data;
	rwrmsg->resultcode.did = DIDmsg_p2req_ramdl_write_resultcode;
	rwrmsg->addr.status = P80211ENUM_msgitem_status_data_ok;
	rwrmsg->len.status = P80211ENUM_msgitem_status_data_ok;
	rwrmsg->data.status = P80211ENUM_msgitem_status_data_ok;
	rwrmsg->resultcode.status = P80211ENUM_msgitem_status_no_value;
	rwrmsg->addr.len = sizeof(u32);
	rwrmsg->len.len = sizeof(u32);
	rwrmsg->data.len = WRITESIZE_MAX;
	rwrmsg->resultcode.len = sizeof(u32);

	/* Send xxx_state(enable) */
	pr_debug("Sending dl_state(enable) message.\n");
	rstmsg->enable.data = P80211ENUM_truth_true;
	rstmsg->exeaddr.data = startaddr;

	result = prism2mgmt_ramdl_state(wlandev, rstmsg);
	if (result) {
		netdev_err(wlandev->netdev,
			   "writeimage state enable failed w/ result=%d, "
			   "aborting download\n", result);
		goto free_result;
	}
	resultcode = rstmsg->resultcode.data;
	if (resultcode != P80211ENUM_resultcode_success) {
		netdev_err(wlandev->netdev,
			   "writeimage()->xxxdl_state msg indicates failure, "
			   "w/ resultcode=%d, aborting download.\n", resultcode);
		result = 1;
		goto free_result;
	}

	/* Now, loop through the data chunks and send WRITESIZE_MAX data */
	for (i = 0; i < nfchunks; i++) {
		nwrites = fchunk[i].len / WRITESIZE_MAX;
		nwrites += (fchunk[i].len % WRITESIZE_MAX) ? 1 : 0;
		curroff = 0;
		for (j = 0; j < nwrites; j++) {
			/* TODO Move this to a separate function */
			int lenleft = fchunk[i].len - (WRITESIZE_MAX * j);

			if (fchunk[i].len > WRITESIZE_MAX)
				currlen = WRITESIZE_MAX;
			else
				currlen = lenleft;
			curroff = j * WRITESIZE_MAX;
			currdaddr = fchunk[i].addr + curroff;
			/* Setup the message */
			rwrmsg->addr.data = currdaddr;
			rwrmsg->len.data = currlen;
			memcpy(rwrmsg->data.data,
			       fchunk[i].data + curroff, currlen);

			/* Send flashdl_write(pda) */
			pr_debug
			    ("Sending xxxdl_write message addr=%06x len=%d.\n",
			     currdaddr, currlen);

			result = prism2mgmt_ramdl_write(wlandev, rwrmsg);

			/* Check the results */
			if (result) {
				netdev_err(wlandev->netdev,
					   "writeimage chunk write failed w/ "
					   "result=%d, aborting download\n", result);
				goto free_result;
			}
			resultcode = rstmsg->resultcode.data;
			if (resultcode != P80211ENUM_resultcode_success) {
				pr_err("writeimage()->xxxdl_write msg indicates failure, "
				       "w/ resultcode=%d, aborting download.\n",
				       resultcode);
				result = 1;
				goto free_result;
			}

		}
	}

	/* Send xxx_state(disable) */
	pr_debug("Sending dl_state(disable) message.\n");
	rstmsg->enable.data = P80211ENUM_truth_false;
	rstmsg->exeaddr.data = 0;

	result = prism2mgmt_ramdl_state(wlandev, rstmsg);
	if (result) {
		netdev_err(wlandev->netdev,
			   "writeimage state disable failed w/ result=%d, "
			   "aborting download\n", result);
		goto free_result;
	}
	resultcode = rstmsg->resultcode.data;
	if (resultcode != P80211ENUM_resultcode_success) {
		netdev_err(wlandev->netdev,
			   "writeimage()->xxxdl_state msg indicates failure, "
			   "w/ resultcode=%d, aborting download.\n", resultcode);
		result = 1;
		goto free_result;
	}

free_result:
	kfree(rstmsg);
	kfree(rwrmsg);
	return result;
}

static int validate_identity(void)
{
	int i;
	int result = 1;
	int trump = 0;

	pr_debug("NIC ID: %#x v%d.%d.%d\n",
		 nicid.id, nicid.major, nicid.minor, nicid.variant);
	pr_debug("MFI ID: %#x v%d %d->%d\n",
		 rfid.id, rfid.variant, rfid.bottom, rfid.top);
	pr_debug("CFI ID: %#x v%d %d->%d\n",
		 macid.id, macid.variant, macid.bottom, macid.top);
	pr_debug("PRI ID: %#x v%d %d->%d\n",
		 priid.id, priid.variant, priid.bottom, priid.top);

	for (i = 0; i < ns3info; i++) {
		switch (s3info[i].type) {
		case 1:
			pr_debug("Version:  ID %#x %d.%d.%d\n",
				 s3info[i].info.version.id,
				 s3info[i].info.version.major,
				 s3info[i].info.version.minor,
				 s3info[i].info.version.variant);
			break;
		case 2:
			pr_debug("Compat: Role %#x Id %#x v%d %d->%d\n",
				 s3info[i].info.compat.role,
				 s3info[i].info.compat.id,
				 s3info[i].info.compat.variant,
				 s3info[i].info.compat.bottom,
				 s3info[i].info.compat.top);

			/* MAC compat range */
			if ((s3info[i].info.compat.role == 1) &&
			    (s3info[i].info.compat.id == 2)) {
				if (s3info[i].info.compat.variant !=
				    macid.variant) {
					result = 2;
				}
			}

			/* PRI compat range */
			if ((s3info[i].info.compat.role == 1) &&
			    (s3info[i].info.compat.id == 3)) {
				if ((s3info[i].info.compat.bottom > priid.top)
				    || (s3info[i].info.compat.top <
					priid.bottom)) {
					result = 3;
				}
			}
			/* SEC compat range */
			if ((s3info[i].info.compat.role == 1) &&
			    (s3info[i].info.compat.id == 4)) {
				/* FIXME: isn't something missing here? */
			}

			break;
		case 3:
			pr_debug("Seq: %#x\n", s3info[i].info.buildseq);

			break;
		case 4:
			pr_debug("Platform:  ID %#x %d.%d.%d\n",
				 s3info[i].info.version.id,
				 s3info[i].info.version.major,
				 s3info[i].info.version.minor,
				 s3info[i].info.version.variant);

			if (nicid.id != s3info[i].info.version.id)
				continue;
			if (nicid.major != s3info[i].info.version.major)
				continue;
			if (nicid.minor != s3info[i].info.version.minor)
				continue;
			if ((nicid.variant != s3info[i].info.version.variant) &&
			    (nicid.id != 0x8008))
				continue;

			trump = 1;
			break;
		case 0x8001:
			pr_debug("name inforec len %d\n", s3info[i].len);

			break;
		default:
			pr_debug("Unknown inforec type %d\n", s3info[i].type);
		}
	}
	/* walk through */

	if (trump && (result != 2))
		result = 0;
	return result;
}
