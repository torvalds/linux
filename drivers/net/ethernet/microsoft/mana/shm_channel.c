// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <net/mana/shm_channel.h>

#define PAGE_FRAME_L48_WIDTH_BYTES 6
#define PAGE_FRAME_L48_WIDTH_BITS (PAGE_FRAME_L48_WIDTH_BYTES * 8)
#define PAGE_FRAME_L48_MASK 0x0000FFFFFFFFFFFF
#define PAGE_FRAME_H4_WIDTH_BITS 4
#define VECTOR_MASK 0xFFFF
#define SHMEM_VF_RESET_STATE ((u32)-1)

#define SMC_MSG_TYPE_ESTABLISH_HWC 1
#define SMC_MSG_TYPE_ESTABLISH_HWC_VERSION 0

#define SMC_MSG_TYPE_DESTROY_HWC 2
#define SMC_MSG_TYPE_DESTROY_HWC_VERSION 0

#define SMC_MSG_DIRECTION_REQUEST 0
#define SMC_MSG_DIRECTION_RESPONSE 1

/* Structures labeled with "HW DATA" are exchanged with the hardware. All of
 * them are naturally aligned and hence don't need __packed.
 */

/* Shared memory channel protocol header
 *
 * msg_type: set on request and response; response matches request.
 * msg_version: newer PF writes back older response (matching request)
 *  older PF acts on latest version known and sets that version in result
 *  (less than request).
 * direction: 0 for request, VF->PF; 1 for response, PF->VF.
 * status: 0 on request,
 *   operation result on response (success = 0, failure = 1 or greater).
 * reset_vf: If set on either establish or destroy request, indicates perform
 *  FLR before/after the operation.
 * owner_is_pf: 1 indicates PF owned, 0 indicates VF owned.
 */
union smc_proto_hdr {
	u32 as_uint32;

	struct {
		u8 msg_type	: 3;
		u8 msg_version	: 3;
		u8 reserved_1	: 1;
		u8 direction	: 1;

		u8 status;

		u8 reserved_2;

		u8 reset_vf	: 1;
		u8 reserved_3	: 6;
		u8 owner_is_pf	: 1;
	};
}; /* HW DATA */

#define SMC_APERTURE_BITS 256
#define SMC_BASIC_UNIT (sizeof(u32))
#define SMC_APERTURE_DWORDS (SMC_APERTURE_BITS / (SMC_BASIC_UNIT * 8))
#define SMC_LAST_DWORD (SMC_APERTURE_DWORDS - 1)

static int mana_smc_poll_register(void __iomem *base, bool reset)
{
	void __iomem *ptr = base + SMC_LAST_DWORD * SMC_BASIC_UNIT;
	u32 last_dword;
	int i;

	/* Poll the hardware for the ownership bit. This should be pretty fast,
	 * but let's do it in a loop just in case the hardware or the PF
	 * driver are temporarily busy.
	 */
	for (i = 0; i < 20 * 1000; i++)  {
		last_dword = readl(ptr);

		/* shmem reads as 0xFFFFFFFF in the reset case */
		if (reset && last_dword == SHMEM_VF_RESET_STATE)
			return 0;

		/* If bit_31 is set, the PF currently owns the SMC. */
		if (!(last_dword & BIT(31)))
			return 0;

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int mana_smc_read_response(struct shm_channel *sc, u32 msg_type,
				  u32 msg_version, bool reset_vf)
{
	void __iomem *base = sc->base;
	union smc_proto_hdr hdr;
	int err;

	/* Wait for PF to respond. */
	err = mana_smc_poll_register(base, reset_vf);
	if (err)
		return err;

	hdr.as_uint32 = readl(base + SMC_LAST_DWORD * SMC_BASIC_UNIT);

	if (reset_vf && hdr.as_uint32 == SHMEM_VF_RESET_STATE)
		return 0;

	/* Validate protocol fields from the PF driver */
	if (hdr.msg_type != msg_type || hdr.msg_version > msg_version ||
	    hdr.direction != SMC_MSG_DIRECTION_RESPONSE) {
		dev_err(sc->dev, "Wrong SMC response 0x%x, type=%d, ver=%d\n",
			hdr.as_uint32, msg_type, msg_version);
		return -EPROTO;
	}

	/* Validate the operation result */
	if (hdr.status != 0) {
		dev_err(sc->dev, "SMC operation failed: 0x%x\n", hdr.status);
		return -EPROTO;
	}

	return 0;
}

void mana_smc_init(struct shm_channel *sc, struct device *dev,
		   void __iomem *base)
{
	sc->dev = dev;
	sc->base = base;
}

int mana_smc_setup_hwc(struct shm_channel *sc, bool reset_vf, u64 eq_addr,
		       u64 cq_addr, u64 rq_addr, u64 sq_addr,
		       u32 eq_msix_index)
{
	union smc_proto_hdr *hdr;
	u16 all_addr_h4bits = 0;
	u16 frame_addr_seq = 0;
	u64 frame_addr = 0;
	u8 shm_buf[32];
	u64 *shmem;
	u32 *dword;
	u8 *ptr;
	int err;
	int i;

	/* Ensure VF already has possession of shared memory */
	err = mana_smc_poll_register(sc->base, false);
	if (err) {
		dev_err(sc->dev, "Timeout when setting up HWC: %d\n", err);
		return err;
	}

	if (!PAGE_ALIGNED(eq_addr) || !PAGE_ALIGNED(cq_addr) ||
	    !PAGE_ALIGNED(rq_addr) || !PAGE_ALIGNED(sq_addr))
		return -EINVAL;

	if ((eq_msix_index & VECTOR_MASK) != eq_msix_index)
		return -EINVAL;

	/* Scheme for packing four addresses and extra info into 256 bits.
	 *
	 * Addresses must be page frame aligned, so only frame address bits
	 * are transferred.
	 *
	 * 52-bit frame addresses are split into the lower 48 bits and upper
	 * 4 bits. Lower 48 bits of 4 address are written sequentially from
	 * the start of the 256-bit shared memory region followed by 16 bits
	 * containing the upper 4 bits of the 4 addresses in sequence.
	 *
	 * A 16 bit EQ vector number fills out the next-to-last 32-bit dword.
	 *
	 * The final 32-bit dword is used for protocol control information as
	 * defined in smc_proto_hdr.
	 */

	memset(shm_buf, 0, sizeof(shm_buf));
	ptr = shm_buf;

	/* EQ addr: low 48 bits of frame address */
	shmem = (u64 *)ptr;
	frame_addr = PHYS_PFN(eq_addr);
	*shmem = frame_addr & PAGE_FRAME_L48_MASK;
	all_addr_h4bits |= (frame_addr >> PAGE_FRAME_L48_WIDTH_BITS) <<
		(frame_addr_seq++ * PAGE_FRAME_H4_WIDTH_BITS);
	ptr += PAGE_FRAME_L48_WIDTH_BYTES;

	/* CQ addr: low 48 bits of frame address */
	shmem = (u64 *)ptr;
	frame_addr = PHYS_PFN(cq_addr);
	*shmem = frame_addr & PAGE_FRAME_L48_MASK;
	all_addr_h4bits |= (frame_addr >> PAGE_FRAME_L48_WIDTH_BITS) <<
		(frame_addr_seq++ * PAGE_FRAME_H4_WIDTH_BITS);
	ptr += PAGE_FRAME_L48_WIDTH_BYTES;

	/* RQ addr: low 48 bits of frame address */
	shmem = (u64 *)ptr;
	frame_addr = PHYS_PFN(rq_addr);
	*shmem = frame_addr & PAGE_FRAME_L48_MASK;
	all_addr_h4bits |= (frame_addr >> PAGE_FRAME_L48_WIDTH_BITS) <<
		(frame_addr_seq++ * PAGE_FRAME_H4_WIDTH_BITS);
	ptr += PAGE_FRAME_L48_WIDTH_BYTES;

	/* SQ addr: low 48 bits of frame address */
	shmem = (u64 *)ptr;
	frame_addr = PHYS_PFN(sq_addr);
	*shmem = frame_addr & PAGE_FRAME_L48_MASK;
	all_addr_h4bits |= (frame_addr >> PAGE_FRAME_L48_WIDTH_BITS) <<
		(frame_addr_seq++ * PAGE_FRAME_H4_WIDTH_BITS);
	ptr += PAGE_FRAME_L48_WIDTH_BYTES;

	/* High 4 bits of the four frame addresses */
	*((u16 *)ptr) = all_addr_h4bits;
	ptr += sizeof(u16);

	/* EQ MSIX vector number */
	*((u16 *)ptr) = (u16)eq_msix_index;
	ptr += sizeof(u16);

	/* 32-bit protocol header in final dword */
	*((u32 *)ptr) = 0;

	hdr = (union smc_proto_hdr *)ptr;
	hdr->msg_type = SMC_MSG_TYPE_ESTABLISH_HWC;
	hdr->msg_version = SMC_MSG_TYPE_ESTABLISH_HWC_VERSION;
	hdr->direction = SMC_MSG_DIRECTION_REQUEST;
	hdr->reset_vf = reset_vf;

	/* Write 256-message buffer to shared memory (final 32-bit write
	 * triggers HW to set possession bit to PF).
	 */
	dword = (u32 *)shm_buf;
	for (i = 0; i < SMC_APERTURE_DWORDS; i++)
		writel(*dword++, sc->base + i * SMC_BASIC_UNIT);

	/* Read shmem response (polling for VF possession) and validate.
	 * For setup, waiting for response on shared memory is not strictly
	 * necessary, since wait occurs later for results to appear in EQE's.
	 */
	err = mana_smc_read_response(sc, SMC_MSG_TYPE_ESTABLISH_HWC,
				     SMC_MSG_TYPE_ESTABLISH_HWC_VERSION,
				     reset_vf);
	if (err) {
		dev_err(sc->dev, "Error when setting up HWC: %d\n", err);
		return err;
	}

	return 0;
}

int mana_smc_teardown_hwc(struct shm_channel *sc, bool reset_vf)
{
	union smc_proto_hdr hdr = {};
	int err;

	/* Ensure already has possession of shared memory */
	err = mana_smc_poll_register(sc->base, false);
	if (err) {
		dev_err(sc->dev, "Timeout when tearing down HWC\n");
		return err;
	}

	/* Set up protocol header for HWC destroy message */
	hdr.msg_type = SMC_MSG_TYPE_DESTROY_HWC;
	hdr.msg_version = SMC_MSG_TYPE_DESTROY_HWC_VERSION;
	hdr.direction = SMC_MSG_DIRECTION_REQUEST;
	hdr.reset_vf = reset_vf;

	/* Write message in high 32 bits of 256-bit shared memory, causing HW
	 * to set possession bit to PF.
	 */
	writel(hdr.as_uint32, sc->base + SMC_LAST_DWORD * SMC_BASIC_UNIT);

	/* Read shmem response (polling for VF possession) and validate.
	 * For teardown, waiting for response is required to ensure hardware
	 * invalidates MST entries before software frees memory.
	 */
	err = mana_smc_read_response(sc, SMC_MSG_TYPE_DESTROY_HWC,
				     SMC_MSG_TYPE_DESTROY_HWC_VERSION,
				     reset_vf);
	if (err) {
		dev_err(sc->dev, "Error when tearing down HWC: %d\n", err);
		return err;
	}

	return 0;
}
