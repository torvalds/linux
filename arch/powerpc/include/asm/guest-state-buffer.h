/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interface based on include/net/netlink.h
 */
#ifndef _ASM_POWERPC_GUEST_STATE_BUFFER_H
#define _ASM_POWERPC_GUEST_STATE_BUFFER_H

#include "asm/hvcall.h"
#include <linux/gfp.h>
#include <linux/bitmap.h>
#include <asm/plpar_wrappers.h>

/**************************************************************************
 * Guest State Buffer Constants
 **************************************************************************/
/* Element without a value and any length */
#define KVMPPC_GSID_BLANK			0x0000
/* Size required for the L0's internal VCPU representation */
#define KVMPPC_GSID_HOST_STATE_SIZE		0x0001
 /* Minimum size for the H_GUEST_RUN_VCPU output buffer */
#define KVMPPC_GSID_RUN_OUTPUT_MIN_SIZE		0x0002
 /* "Logical" PVR value as defined in the PAPR */
#define KVMPPC_GSID_LOGICAL_PVR			0x0003
 /* L0 relative timebase offset */
#define KVMPPC_GSID_TB_OFFSET			0x0004
 /* Partition Scoped Page Table Info */
#define KVMPPC_GSID_PARTITION_TABLE		0x0005
 /* Process Table Info */
#define KVMPPC_GSID_PROCESS_TABLE		0x0006

/* Guest Management Heap Size */
#define KVMPPC_GSID_L0_GUEST_HEAP		0x0800

/* Guest Management Heap Max Size */
#define KVMPPC_GSID_L0_GUEST_HEAP_MAX		0x0801

/* Guest Pagetable Size */
#define KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE	0x0802

/* Guest Pagetable Max Size */
#define KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE_MAX	0x0803

/* Guest Pagetable Reclaim in bytes */
#define KVMPPC_GSID_L0_GUEST_PGTABLE_RECLAIM	0x0804

/* H_GUEST_RUN_VCPU input buffer Info */
#define KVMPPC_GSID_RUN_INPUT			0x0C00
/* H_GUEST_RUN_VCPU output buffer Info */
#define KVMPPC_GSID_RUN_OUTPUT			0x0C01
#define KVMPPC_GSID_VPA				0x0C02

#define KVMPPC_GSID_GPR(x)			(0x1000 + (x))
#define KVMPPC_GSID_HDEC_EXPIRY_TB		0x1020
#define KVMPPC_GSID_NIA				0x1021
#define KVMPPC_GSID_MSR				0x1022
#define KVMPPC_GSID_LR				0x1023
#define KVMPPC_GSID_XER				0x1024
#define KVMPPC_GSID_CTR				0x1025
#define KVMPPC_GSID_CFAR			0x1026
#define KVMPPC_GSID_SRR0			0x1027
#define KVMPPC_GSID_SRR1			0x1028
#define KVMPPC_GSID_DAR				0x1029
#define KVMPPC_GSID_DEC_EXPIRY_TB		0x102A
#define KVMPPC_GSID_VTB				0x102B
#define KVMPPC_GSID_LPCR			0x102C
#define KVMPPC_GSID_HFSCR			0x102D
#define KVMPPC_GSID_FSCR			0x102E
#define KVMPPC_GSID_FPSCR			0x102F
#define KVMPPC_GSID_DAWR0			0x1030
#define KVMPPC_GSID_DAWR1			0x1031
#define KVMPPC_GSID_CIABR			0x1032
#define KVMPPC_GSID_PURR			0x1033
#define KVMPPC_GSID_SPURR			0x1034
#define KVMPPC_GSID_IC				0x1035
#define KVMPPC_GSID_SPRG0			0x1036
#define KVMPPC_GSID_SPRG1			0x1037
#define KVMPPC_GSID_SPRG2			0x1038
#define KVMPPC_GSID_SPRG3			0x1039
#define KVMPPC_GSID_PPR				0x103A
#define KVMPPC_GSID_MMCR(x)			(0x103B + (x))
#define KVMPPC_GSID_MMCRA			0x103F
#define KVMPPC_GSID_SIER(x)			(0x1040 + (x))
#define KVMPPC_GSID_BESCR			0x1043
#define KVMPPC_GSID_EBBHR			0x1044
#define KVMPPC_GSID_EBBRR			0x1045
#define KVMPPC_GSID_AMR				0x1046
#define KVMPPC_GSID_IAMR			0x1047
#define KVMPPC_GSID_AMOR			0x1048
#define KVMPPC_GSID_UAMOR			0x1049
#define KVMPPC_GSID_SDAR			0x104A
#define KVMPPC_GSID_SIAR			0x104B
#define KVMPPC_GSID_DSCR			0x104C
#define KVMPPC_GSID_TAR				0x104D
#define KVMPPC_GSID_DEXCR			0x104E
#define KVMPPC_GSID_HDEXCR			0x104F
#define KVMPPC_GSID_HASHKEYR			0x1050
#define KVMPPC_GSID_HASHPKEYR			0x1051
#define KVMPPC_GSID_CTRL			0x1052
#define KVMPPC_GSID_DPDES			0x1053

#define KVMPPC_GSID_CR				0x2000
#define KVMPPC_GSID_PIDR			0x2001
#define KVMPPC_GSID_DSISR			0x2002
#define KVMPPC_GSID_VSCR			0x2003
#define KVMPPC_GSID_VRSAVE			0x2004
#define KVMPPC_GSID_DAWRX0			0x2005
#define KVMPPC_GSID_DAWRX1			0x2006
#define KVMPPC_GSID_PMC(x)			(0x2007 + (x))
#define KVMPPC_GSID_WORT			0x200D
#define KVMPPC_GSID_PSPB			0x200E

#define KVMPPC_GSID_VSRS(x)			(0x3000 + (x))

#define KVMPPC_GSID_HDAR			0xF000
#define KVMPPC_GSID_HDSISR			0xF001
#define KVMPPC_GSID_HEIR			0xF002
#define KVMPPC_GSID_ASDR			0xF003

#define KVMPPC_GSE_GUESTWIDE_START KVMPPC_GSID_BLANK
#define KVMPPC_GSE_GUESTWIDE_END KVMPPC_GSID_PROCESS_TABLE
#define KVMPPC_GSE_GUESTWIDE_COUNT \
	(KVMPPC_GSE_GUESTWIDE_END - KVMPPC_GSE_GUESTWIDE_START + 1)

#define KVMPPC_GSE_HOSTWIDE_START KVMPPC_GSID_L0_GUEST_HEAP
#define KVMPPC_GSE_HOSTWIDE_END KVMPPC_GSID_L0_GUEST_PGTABLE_RECLAIM
#define KVMPPC_GSE_HOSTWIDE_COUNT \
	(KVMPPC_GSE_HOSTWIDE_END - KVMPPC_GSE_HOSTWIDE_START + 1)

#define KVMPPC_GSE_META_START KVMPPC_GSID_RUN_INPUT
#define KVMPPC_GSE_META_END KVMPPC_GSID_VPA
#define KVMPPC_GSE_META_COUNT (KVMPPC_GSE_META_END - KVMPPC_GSE_META_START + 1)

#define KVMPPC_GSE_DW_REGS_START KVMPPC_GSID_GPR(0)
#define KVMPPC_GSE_DW_REGS_END KVMPPC_GSID_DPDES
#define KVMPPC_GSE_DW_REGS_COUNT \
	(KVMPPC_GSE_DW_REGS_END - KVMPPC_GSE_DW_REGS_START + 1)

#define KVMPPC_GSE_W_REGS_START KVMPPC_GSID_CR
#define KVMPPC_GSE_W_REGS_END KVMPPC_GSID_PSPB
#define KVMPPC_GSE_W_REGS_COUNT \
	(KVMPPC_GSE_W_REGS_END - KVMPPC_GSE_W_REGS_START + 1)

#define KVMPPC_GSE_VSRS_START KVMPPC_GSID_VSRS(0)
#define KVMPPC_GSE_VSRS_END KVMPPC_GSID_VSRS(63)
#define KVMPPC_GSE_VSRS_COUNT (KVMPPC_GSE_VSRS_END - KVMPPC_GSE_VSRS_START + 1)

#define KVMPPC_GSE_INTR_REGS_START KVMPPC_GSID_HDAR
#define KVMPPC_GSE_INTR_REGS_END KVMPPC_GSID_ASDR
#define KVMPPC_GSE_INTR_REGS_COUNT \
	(KVMPPC_GSE_INTR_REGS_END - KVMPPC_GSE_INTR_REGS_START + 1)

#define KVMPPC_GSE_IDEN_COUNT                                 \
	(KVMPPC_GSE_HOSTWIDE_COUNT + \
	 KVMPPC_GSE_GUESTWIDE_COUNT + KVMPPC_GSE_META_COUNT + \
	 KVMPPC_GSE_DW_REGS_COUNT + KVMPPC_GSE_W_REGS_COUNT + \
	 KVMPPC_GSE_VSRS_COUNT + KVMPPC_GSE_INTR_REGS_COUNT)

/**
 * Ranges of guest state buffer elements
 */
enum {
	KVMPPC_GS_CLASS_GUESTWIDE = 0x01,
	KVMPPC_GS_CLASS_HOSTWIDE = 0x02,
	KVMPPC_GS_CLASS_META = 0x04,
	KVMPPC_GS_CLASS_DWORD_REG = 0x08,
	KVMPPC_GS_CLASS_WORD_REG = 0x10,
	KVMPPC_GS_CLASS_VECTOR = 0x18,
	KVMPPC_GS_CLASS_INTR = 0x20,
};

/**
 * Types of guest state buffer elements
 */
enum {
	KVMPPC_GSE_BE32,
	KVMPPC_GSE_BE64,
	KVMPPC_GSE_VEC128,
	KVMPPC_GSE_PARTITION_TABLE,
	KVMPPC_GSE_PROCESS_TABLE,
	KVMPPC_GSE_BUFFER,
	__KVMPPC_GSE_TYPE_MAX,
};

/**
 * Flags for guest state elements
 */
enum {
	KVMPPC_GS_FLAGS_WIDE = 0x01,
	KVMPPC_GS_FLAGS_HOST_WIDE = 0x02,
};

/**
 * struct kvmppc_gs_part_table - deserialized partition table information
 * element
 * @address: start of the partition table
 * @ea_bits: number of bits in the effective address
 * @gpd_size: root page directory size
 */
struct kvmppc_gs_part_table {
	u64 address;
	u64 ea_bits;
	u64 gpd_size;
};

/**
 * struct kvmppc_gs_proc_table - deserialized process table information element
 * @address: start of the process table
 * @gpd_size: process table size
 */
struct kvmppc_gs_proc_table {
	u64 address;
	u64 gpd_size;
};

/**
 * struct kvmppc_gs_buff_info - deserialized meta guest state buffer information
 * @address: start of the guest state buffer
 * @size: size of the guest state buffer
 */
struct kvmppc_gs_buff_info {
	u64 address;
	u64 size;
};

/**
 * struct kvmppc_gs_header - serialized guest state buffer header
 * @nelem: count of guest state elements in the buffer
 * @data: start of the stream of elements in the buffer
 */
struct kvmppc_gs_header {
	__be32 nelems;
	char data[];
} __packed;

/**
 * struct kvmppc_gs_elem - serialized guest state buffer element
 * @iden: Guest State ID
 * @len: length of data
 * @data: the guest state buffer element's value
 */
struct kvmppc_gs_elem {
	__be16 iden;
	__be16 len;
	char data[];
} __packed;

/**
 * struct kvmppc_gs_buff - a guest state buffer with metadata.
 * @capacity: total length of the buffer
 * @len: current length of the elements and header
 * @guest_id: guest id associated with the buffer
 * @vcpu_id: vcpu_id associated with the buffer
 * @hdr: the serialised guest state buffer
 */
struct kvmppc_gs_buff {
	size_t capacity;
	size_t len;
	unsigned long guest_id;
	unsigned long vcpu_id;
	struct kvmppc_gs_header *hdr;
};

/**
 * struct kvmppc_gs_bitmap - a bitmap for element ids
 * @bitmap: a bitmap large enough for all Guest State IDs
 */
struct kvmppc_gs_bitmap {
	/* private: */
	DECLARE_BITMAP(bitmap, KVMPPC_GSE_IDEN_COUNT);
};

/**
 * struct kvmppc_gs_parser - a map of element ids to locations in a buffer
 * @iterator: bitmap used for iterating
 * @gses: contains the pointers to elements
 *
 * A guest state parser is used for deserialising a guest state buffer.
 * Given a buffer, it then allows looking up guest state elements using
 * a guest state id.
 */
struct kvmppc_gs_parser {
	/* private: */
	struct kvmppc_gs_bitmap iterator;
	struct kvmppc_gs_elem *gses[KVMPPC_GSE_IDEN_COUNT];
};

enum {
	GSM_GUEST_WIDE = 0x1,
	GSM_SEND = 0x2,
	GSM_RECEIVE = 0x4,
	GSM_GSB_OWNER = 0x8,
};

struct kvmppc_gs_msg;

/**
 * struct kvmppc_gs_msg_ops - guest state message behavior
 * @get_size: maximum size required for the message data
 * @fill_info: serializes to the guest state buffer format
 * @refresh_info: dserializes from the guest state buffer format
 */
struct kvmppc_gs_msg_ops {
	size_t (*get_size)(struct kvmppc_gs_msg *gsm);
	int (*fill_info)(struct kvmppc_gs_buff *gsb, struct kvmppc_gs_msg *gsm);
	int (*refresh_info)(struct kvmppc_gs_msg *gsm,
			    struct kvmppc_gs_buff *gsb);
};

/**
 * struct kvmppc_gs_msg - a guest state message
 * @bitmap: the guest state ids that should be included
 * @ops: modify message behavior for reading and writing to buffers
 * @flags: host wide, guest wide or thread wide
 * @data: location where buffer data will be written to or from.
 *
 * A guest state message is allows flexibility in sending in receiving data
 * in a guest state buffer format.
 */
struct kvmppc_gs_msg {
	struct kvmppc_gs_bitmap bitmap;
	struct kvmppc_gs_msg_ops *ops;
	unsigned long flags;
	void *data;
};

/**************************************************************************
 * Guest State IDs
 **************************************************************************/

u16 kvmppc_gsid_size(u16 iden);
unsigned long kvmppc_gsid_flags(u16 iden);
u64 kvmppc_gsid_mask(u16 iden);

/**************************************************************************
 * Guest State Buffers
 **************************************************************************/
struct kvmppc_gs_buff *kvmppc_gsb_new(size_t size, unsigned long guest_id,
				      unsigned long vcpu_id, gfp_t flags);
void kvmppc_gsb_free(struct kvmppc_gs_buff *gsb);
void *kvmppc_gsb_put(struct kvmppc_gs_buff *gsb, size_t size);
int kvmppc_gsb_send(struct kvmppc_gs_buff *gsb, unsigned long flags);
int kvmppc_gsb_recv(struct kvmppc_gs_buff *gsb, unsigned long flags);

/**
 * kvmppc_gsb_header() - the header of a guest state buffer
 * @gsb: guest state buffer
 *
 * Returns a pointer to the buffer header.
 */
static inline struct kvmppc_gs_header *
kvmppc_gsb_header(struct kvmppc_gs_buff *gsb)
{
	return gsb->hdr;
}

/**
 * kvmppc_gsb_data() - the elements of a guest state buffer
 * @gsb: guest state buffer
 *
 * Returns a pointer to the first element of the buffer data.
 */
static inline struct kvmppc_gs_elem *kvmppc_gsb_data(struct kvmppc_gs_buff *gsb)
{
	return (struct kvmppc_gs_elem *)kvmppc_gsb_header(gsb)->data;
}

/**
 * kvmppc_gsb_len() - the current length of a guest state buffer
 * @gsb: guest state buffer
 *
 * Returns the length including the header of a buffer.
 */
static inline size_t kvmppc_gsb_len(struct kvmppc_gs_buff *gsb)
{
	return gsb->len;
}

/**
 * kvmppc_gsb_capacity() - the capacity of a guest state buffer
 * @gsb: guest state buffer
 *
 * Returns the capacity of a buffer.
 */
static inline size_t kvmppc_gsb_capacity(struct kvmppc_gs_buff *gsb)
{
	return gsb->capacity;
}

/**
 * kvmppc_gsb_paddress() - the physical address of buffer
 * @gsb: guest state buffer
 *
 * Returns the physical address of the buffer.
 */
static inline u64 kvmppc_gsb_paddress(struct kvmppc_gs_buff *gsb)
{
	return __pa(kvmppc_gsb_header(gsb));
}

/**
 * kvmppc_gsb_nelems() - the number of elements in a buffer
 * @gsb: guest state buffer
 *
 * Returns the number of elements in a buffer
 */
static inline u32 kvmppc_gsb_nelems(struct kvmppc_gs_buff *gsb)
{
	return be32_to_cpu(kvmppc_gsb_header(gsb)->nelems);
}

/**
 * kvmppc_gsb_reset() - empty a guest state buffer
 * @gsb: guest state buffer
 *
 * Reset the number of elements and length of buffer to empty.
 */
static inline void kvmppc_gsb_reset(struct kvmppc_gs_buff *gsb)
{
	kvmppc_gsb_header(gsb)->nelems = cpu_to_be32(0);
	gsb->len = sizeof(struct kvmppc_gs_header);
}

/**
 * kvmppc_gsb_data_len() - the length of a buffer excluding the header
 * @gsb: guest state buffer
 *
 * Returns the length of a buffer excluding the header
 */
static inline size_t kvmppc_gsb_data_len(struct kvmppc_gs_buff *gsb)
{
	return gsb->len - sizeof(struct kvmppc_gs_header);
}

/**
 * kvmppc_gsb_data_cap() - the capacity of a buffer excluding the header
 * @gsb: guest state buffer
 *
 * Returns the capacity of a buffer excluding the header
 */
static inline size_t kvmppc_gsb_data_cap(struct kvmppc_gs_buff *gsb)
{
	return gsb->capacity - sizeof(struct kvmppc_gs_header);
}

/**
 * kvmppc_gsb_for_each_elem - iterate over the elements in a buffer
 * @i: loop counter
 * @pos: set to current element
 * @gsb: guest state buffer
 * @rem: initialized to buffer capacity, holds bytes currently remaining in
 *  stream
 */
#define kvmppc_gsb_for_each_elem(i, pos, gsb, rem)               \
	kvmppc_gse_for_each_elem(i, kvmppc_gsb_nelems(gsb), pos, \
				 kvmppc_gsb_data(gsb),           \
				 kvmppc_gsb_data_cap(gsb), rem)

/**************************************************************************
 * Guest State Elements
 **************************************************************************/

/**
 * kvmppc_gse_iden() - guest state ID of element
 * @gse: guest state element
 *
 * Return the guest state ID in host endianness.
 */
static inline u16 kvmppc_gse_iden(const struct kvmppc_gs_elem *gse)
{
	return be16_to_cpu(gse->iden);
}

/**
 * kvmppc_gse_len() - length of guest state element data
 * @gse: guest state element
 *
 * Returns the length of guest state element data
 */
static inline u16 kvmppc_gse_len(const struct kvmppc_gs_elem *gse)
{
	return be16_to_cpu(gse->len);
}

/**
 * kvmppc_gse_total_len() - total length of guest state element
 * @gse: guest state element
 *
 * Returns the length of the data plus the ID and size header.
 */
static inline u16 kvmppc_gse_total_len(const struct kvmppc_gs_elem *gse)
{
	return be16_to_cpu(gse->len) + sizeof(*gse);
}

/**
 * kvmppc_gse_total_size() - space needed for a given data length
 * @size: data length
 *
 * Returns size plus the space needed for the ID and size header.
 */
static inline u16 kvmppc_gse_total_size(u16 size)
{
	return sizeof(struct kvmppc_gs_elem) + size;
}

/**
 * kvmppc_gse_data() - pointer to data of a guest state element
 * @gse: guest state element
 *
 * Returns a pointer to the beginning of guest state element data.
 */
static inline void *kvmppc_gse_data(const struct kvmppc_gs_elem *gse)
{
	return (void *)gse->data;
}

/**
 * kvmppc_gse_ok() - checks space exists for guest state element
 * @gse: guest state element
 * @remaining: bytes of space remaining
 *
 * Returns true if the guest state element can fit in remaining space.
 */
static inline bool kvmppc_gse_ok(const struct kvmppc_gs_elem *gse,
				 int remaining)
{
	return remaining >= kvmppc_gse_total_len(gse);
}

/**
 * kvmppc_gse_next() - iterate to the next guest state element in a stream
 * @gse: stream of guest state elements
 * @remaining: length of the guest element stream
 *
 * Returns the next guest state element in a stream of elements. The length of
 * the stream is updated in remaining.
 */
static inline struct kvmppc_gs_elem *
kvmppc_gse_next(const struct kvmppc_gs_elem *gse, int *remaining)
{
	int len = sizeof(*gse) + kvmppc_gse_len(gse);

	*remaining -= len;
	return (struct kvmppc_gs_elem *)(gse->data + kvmppc_gse_len(gse));
}

/**
 * kvmppc_gse_for_each_elem - iterate over a stream of guest state elements
 * @i: loop counter
 * @max: number of elements
 * @pos: set to current element
 * @head: head of elements
 * @len: length of the stream
 * @rem: initialized to len, holds bytes currently remaining elements
 */
#define kvmppc_gse_for_each_elem(i, max, pos, head, len, rem)                  \
	for (i = 0, pos = head, rem = len; kvmppc_gse_ok(pos, rem) && i < max; \
	     pos = kvmppc_gse_next(pos, &(rem)), i++)

int __kvmppc_gse_put(struct kvmppc_gs_buff *gsb, u16 iden, u16 size,
		     const void *data);
int kvmppc_gse_parse(struct kvmppc_gs_parser *gsp, struct kvmppc_gs_buff *gsb);

/**
 * kvmppc_gse_put_be32() - add a be32 guest state element to a buffer
 * @gsb: guest state buffer to add element to
 * @iden: guest state ID
 * @val: big endian value
 */
static inline int kvmppc_gse_put_be32(struct kvmppc_gs_buff *gsb, u16 iden,
				      __be32 val)
{
	__be32 tmp;

	tmp = val;
	return __kvmppc_gse_put(gsb, iden, sizeof(__be32), &tmp);
}

/**
 * kvmppc_gse_put_u32() - add a host endian 32bit int guest state element to a
 * buffer
 * @gsb: guest state buffer to add element to
 * @iden: guest state ID
 * @val: host endian value
 */
static inline int kvmppc_gse_put_u32(struct kvmppc_gs_buff *gsb, u16 iden,
				     u32 val)
{
	__be32 tmp;

	val &= kvmppc_gsid_mask(iden);
	tmp = cpu_to_be32(val);
	return kvmppc_gse_put_be32(gsb, iden, tmp);
}

/**
 * kvmppc_gse_put_be64() - add a be64 guest state element to a buffer
 * @gsb: guest state buffer to add element to
 * @iden: guest state ID
 * @val: big endian value
 */
static inline int kvmppc_gse_put_be64(struct kvmppc_gs_buff *gsb, u16 iden,
				      __be64 val)
{
	__be64 tmp;

	tmp = val;
	return __kvmppc_gse_put(gsb, iden, sizeof(__be64), &tmp);
}

/**
 * kvmppc_gse_put_u64() - add a host endian 64bit guest state element to a
 * buffer
 * @gsb: guest state buffer to add element to
 * @iden: guest state ID
 * @val: host endian value
 */
static inline int kvmppc_gse_put_u64(struct kvmppc_gs_buff *gsb, u16 iden,
				     u64 val)
{
	__be64 tmp;

	val &= kvmppc_gsid_mask(iden);
	tmp = cpu_to_be64(val);
	return kvmppc_gse_put_be64(gsb, iden, tmp);
}

/**
 * __kvmppc_gse_put_reg() - add a register type guest state element to a buffer
 * @gsb: guest state buffer to add element to
 * @iden: guest state ID
 * @val: host endian value
 *
 * Adds a register type guest state element. Uses the guest state ID for
 * determining the length of the guest element. If the guest state ID has
 * bits that can not be set they will be cleared.
 */
static inline int __kvmppc_gse_put_reg(struct kvmppc_gs_buff *gsb, u16 iden,
				       u64 val)
{
	val &= kvmppc_gsid_mask(iden);
	if (kvmppc_gsid_size(iden) == sizeof(u64))
		return kvmppc_gse_put_u64(gsb, iden, val);

	if (kvmppc_gsid_size(iden) == sizeof(u32)) {
		u32 tmp;

		tmp = (u32)val;
		if (tmp != val)
			return -EINVAL;

		return kvmppc_gse_put_u32(gsb, iden, tmp);
	}
	return -EINVAL;
}

/**
 * kvmppc_gse_put_vector128() - add a vector guest state element to a buffer
 * @gsb: guest state buffer to add element to
 * @iden: guest state ID
 * @val: 16 byte vector value
 */
static inline int kvmppc_gse_put_vector128(struct kvmppc_gs_buff *gsb, u16 iden,
					   vector128 *val)
{
	__be64 tmp[2] = { 0 };
	union {
		__vector128 v;
		u64 dw[2];
	} u;

	u.v = *val;
	tmp[0] = cpu_to_be64(u.dw[TS_FPROFFSET]);
#ifdef CONFIG_VSX
	tmp[1] = cpu_to_be64(u.dw[TS_VSRLOWOFFSET]);
#endif
	return __kvmppc_gse_put(gsb, iden, sizeof(tmp), &tmp);
}

/**
 * kvmppc_gse_put_part_table() - add a partition table guest state element to a
 * buffer
 * @gsb: guest state buffer to add element to
 * @iden: guest state ID
 * @val: partition table value
 */
static inline int kvmppc_gse_put_part_table(struct kvmppc_gs_buff *gsb,
					    u16 iden,
					    struct kvmppc_gs_part_table val)
{
	__be64 tmp[3];

	tmp[0] = cpu_to_be64(val.address);
	tmp[1] = cpu_to_be64(val.ea_bits);
	tmp[2] = cpu_to_be64(val.gpd_size);
	return __kvmppc_gse_put(gsb, KVMPPC_GSID_PARTITION_TABLE, sizeof(tmp),
				&tmp);
}

/**
 * kvmppc_gse_put_proc_table() - add a process table guest state element to a
 * buffer
 * @gsb: guest state buffer to add element to
 * @iden: guest state ID
 * @val: process table value
 */
static inline int kvmppc_gse_put_proc_table(struct kvmppc_gs_buff *gsb,
					    u16 iden,
					    struct kvmppc_gs_proc_table val)
{
	__be64 tmp[2];

	tmp[0] = cpu_to_be64(val.address);
	tmp[1] = cpu_to_be64(val.gpd_size);
	return __kvmppc_gse_put(gsb, KVMPPC_GSID_PROCESS_TABLE, sizeof(tmp),
				&tmp);
}

/**
 * kvmppc_gse_put_buff_info() - adds a GSB description guest state element to a
 * buffer
 * @gsb: guest state buffer to add element to
 * @iden: guest state ID
 * @val: guest state buffer description value
 */
static inline int kvmppc_gse_put_buff_info(struct kvmppc_gs_buff *gsb, u16 iden,
					   struct kvmppc_gs_buff_info val)
{
	__be64 tmp[2];

	tmp[0] = cpu_to_be64(val.address);
	tmp[1] = cpu_to_be64(val.size);
	return __kvmppc_gse_put(gsb, iden, sizeof(tmp), &tmp);
}

int __kvmppc_gse_put(struct kvmppc_gs_buff *gsb, u16 iden, u16 size,
		     const void *data);

/**
 * kvmppc_gse_get_be32() - return the data of a be32 element
 * @gse: guest state element
 */
static inline __be32 kvmppc_gse_get_be32(const struct kvmppc_gs_elem *gse)
{
	if (WARN_ON(kvmppc_gse_len(gse) != sizeof(__be32)))
		return 0;
	return *(__be32 *)kvmppc_gse_data(gse);
}

/**
 * kvmppc_gse_get_u32() - return the data of a be32 element in host endianness
 * @gse: guest state element
 */
static inline u32 kvmppc_gse_get_u32(const struct kvmppc_gs_elem *gse)
{
	return be32_to_cpu(kvmppc_gse_get_be32(gse));
}

/**
 * kvmppc_gse_get_be64() - return the data of a be64 element
 * @gse: guest state element
 */
static inline __be64 kvmppc_gse_get_be64(const struct kvmppc_gs_elem *gse)
{
	if (WARN_ON(kvmppc_gse_len(gse) != sizeof(__be64)))
		return 0;
	return *(__be64 *)kvmppc_gse_data(gse);
}

/**
 * kvmppc_gse_get_u64() - return the data of a be64 element in host endianness
 * @gse: guest state element
 */
static inline u64 kvmppc_gse_get_u64(const struct kvmppc_gs_elem *gse)
{
	return be64_to_cpu(kvmppc_gse_get_be64(gse));
}

/**
 * kvmppc_gse_get_vector128() - return the data of a vector element
 * @gse: guest state element
 */
static inline void kvmppc_gse_get_vector128(const struct kvmppc_gs_elem *gse,
					    vector128 *v)
{
	union {
		__vector128 v;
		u64 dw[2];
	} u = { 0 };
	__be64 *src;

	if (WARN_ON(kvmppc_gse_len(gse) != sizeof(__vector128)))
		*v = u.v;

	src = (__be64 *)kvmppc_gse_data(gse);
	u.dw[TS_FPROFFSET] = be64_to_cpu(src[0]);
#ifdef CONFIG_VSX
	u.dw[TS_VSRLOWOFFSET] = be64_to_cpu(src[1]);
#endif
	*v = u.v;
}

/**************************************************************************
 * Guest State Bitmap
 **************************************************************************/

bool kvmppc_gsbm_test(struct kvmppc_gs_bitmap *gsbm, u16 iden);
void kvmppc_gsbm_set(struct kvmppc_gs_bitmap *gsbm, u16 iden);
void kvmppc_gsbm_clear(struct kvmppc_gs_bitmap *gsbm, u16 iden);
u16 kvmppc_gsbm_next(struct kvmppc_gs_bitmap *gsbm, u16 prev);

/**
 * kvmppc_gsbm_zero - zero the entire bitmap
 * @gsbm: guest state buffer bitmap
 */
static inline void kvmppc_gsbm_zero(struct kvmppc_gs_bitmap *gsbm)
{
	bitmap_zero(gsbm->bitmap, KVMPPC_GSE_IDEN_COUNT);
}

/**
 * kvmppc_gsbm_fill - fill the entire bitmap
 * @gsbm: guest state buffer bitmap
 */
static inline void kvmppc_gsbm_fill(struct kvmppc_gs_bitmap *gsbm)
{
	bitmap_fill(gsbm->bitmap, KVMPPC_GSE_IDEN_COUNT);
	clear_bit(0, gsbm->bitmap);
}

/**
 * kvmppc_gsbm_for_each - iterate the present guest state IDs
 * @gsbm: guest state buffer bitmap
 * @iden: current guest state ID
 */
#define kvmppc_gsbm_for_each(gsbm, iden)                  \
	for (iden = kvmppc_gsbm_next(gsbm, 0); iden != 0; \
	     iden = kvmppc_gsbm_next(gsbm, iden))

/**************************************************************************
 * Guest State Parser
 **************************************************************************/

void kvmppc_gsp_insert(struct kvmppc_gs_parser *gsp, u16 iden,
		       struct kvmppc_gs_elem *gse);
struct kvmppc_gs_elem *kvmppc_gsp_lookup(struct kvmppc_gs_parser *gsp,
					 u16 iden);

/**
 * kvmppc_gsp_for_each - iterate the <guest state IDs, guest state element>
 * pairs
 * @gsp: guest state buffer bitmap
 * @iden: current guest state ID
 * @gse: guest state element
 */
#define kvmppc_gsp_for_each(gsp, iden, gse)                              \
	for (iden = kvmppc_gsbm_next(&(gsp)->iterator, 0),               \
	    gse = kvmppc_gsp_lookup((gsp), iden);                        \
	     iden != 0; iden = kvmppc_gsbm_next(&(gsp)->iterator, iden), \
	    gse = kvmppc_gsp_lookup((gsp), iden))

/**************************************************************************
 * Guest State Message
 **************************************************************************/

/**
 * kvmppc_gsm_for_each - iterate the guest state IDs included in a guest state
 * message
 * @gsp: guest state buffer bitmap
 * @iden: current guest state ID
 * @gse: guest state element
 */
#define kvmppc_gsm_for_each(gsm, iden)                            \
	for (iden = kvmppc_gsbm_next(&gsm->bitmap, 0); iden != 0; \
	     iden = kvmppc_gsbm_next(&gsm->bitmap, iden))

int kvmppc_gsm_init(struct kvmppc_gs_msg *mgs, struct kvmppc_gs_msg_ops *ops,
		    void *data, unsigned long flags);

struct kvmppc_gs_msg *kvmppc_gsm_new(struct kvmppc_gs_msg_ops *ops, void *data,
				     unsigned long flags, gfp_t gfp_flags);
void kvmppc_gsm_free(struct kvmppc_gs_msg *gsm);
size_t kvmppc_gsm_size(struct kvmppc_gs_msg *gsm);
int kvmppc_gsm_fill_info(struct kvmppc_gs_msg *gsm, struct kvmppc_gs_buff *gsb);
int kvmppc_gsm_refresh_info(struct kvmppc_gs_msg *gsm,
			    struct kvmppc_gs_buff *gsb);

/**
 * kvmppc_gsm_include - indicate a guest state ID should be included when
 * serializing
 * @gsm: guest state message
 * @iden: guest state ID
 */
static inline void kvmppc_gsm_include(struct kvmppc_gs_msg *gsm, u16 iden)
{
	kvmppc_gsbm_set(&gsm->bitmap, iden);
}

/**
 * kvmppc_gsm_includes - check if a guest state ID will be included when
 * serializing
 * @gsm: guest state message
 * @iden: guest state ID
 */
static inline bool kvmppc_gsm_includes(struct kvmppc_gs_msg *gsm, u16 iden)
{
	return kvmppc_gsbm_test(&gsm->bitmap, iden);
}

/**
 * kvmppc_gsm_includes - indicate all guest state IDs should be included when
 * serializing
 * @gsm: guest state message
 * @iden: guest state ID
 */
static inline void kvmppc_gsm_include_all(struct kvmppc_gs_msg *gsm)
{
	kvmppc_gsbm_fill(&gsm->bitmap);
}

/**
 * kvmppc_gsm_include - clear the guest state IDs that should be included when
 * serializing
 * @gsm: guest state message
 */
static inline void kvmppc_gsm_reset(struct kvmppc_gs_msg *gsm)
{
	kvmppc_gsbm_zero(&gsm->bitmap);
}

/**
 * kvmppc_gsb_receive_data - flexibly update values from a guest state buffer
 * @gsb: guest state buffer
 * @gsm: guest state message
 *
 * Requests updated values for the guest state values included in the guest
 * state message. The guest state message will then deserialize the guest state
 * buffer.
 */
static inline int kvmppc_gsb_receive_data(struct kvmppc_gs_buff *gsb,
					  struct kvmppc_gs_msg *gsm)
{
	int rc;

	kvmppc_gsb_reset(gsb);
	rc = kvmppc_gsm_fill_info(gsm, gsb);
	if (rc < 0)
		return rc;

	rc = kvmppc_gsb_recv(gsb, gsm->flags);
	if (rc < 0)
		return rc;

	rc = kvmppc_gsm_refresh_info(gsm, gsb);
	if (rc < 0)
		return rc;
	return 0;
}

/**
 * kvmppc_gsb_recv - receive a single guest state ID
 * @gsb: guest state buffer
 * @gsm: guest state message
 * @iden: guest state identity
 */
static inline int kvmppc_gsb_receive_datum(struct kvmppc_gs_buff *gsb,
					   struct kvmppc_gs_msg *gsm, u16 iden)
{
	int rc;

	kvmppc_gsm_include(gsm, iden);
	rc = kvmppc_gsb_receive_data(gsb, gsm);
	if (rc < 0)
		return rc;
	kvmppc_gsm_reset(gsm);
	return 0;
}

/**
 * kvmppc_gsb_send_data - flexibly send values from a guest state buffer
 * @gsb: guest state buffer
 * @gsm: guest state message
 *
 * Sends the guest state values included in the guest state message.
 */
static inline int kvmppc_gsb_send_data(struct kvmppc_gs_buff *gsb,
				       struct kvmppc_gs_msg *gsm)
{
	int rc;

	kvmppc_gsb_reset(gsb);
	rc = kvmppc_gsm_fill_info(gsm, gsb);
	if (rc < 0)
		return rc;
	rc = kvmppc_gsb_send(gsb, gsm->flags);

	return rc;
}

/**
 * kvmppc_gsb_recv - send a single guest state ID
 * @gsb: guest state buffer
 * @gsm: guest state message
 * @iden: guest state identity
 */
static inline int kvmppc_gsb_send_datum(struct kvmppc_gs_buff *gsb,
					struct kvmppc_gs_msg *gsm, u16 iden)
{
	int rc;

	kvmppc_gsm_include(gsm, iden);
	rc = kvmppc_gsb_send_data(gsb, gsm);
	if (rc < 0)
		return rc;
	kvmppc_gsm_reset(gsm);
	return 0;
}

#endif /* _ASM_POWERPC_GUEST_STATE_BUFFER_H */
