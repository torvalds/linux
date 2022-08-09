/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_ITEM_H
#define _MLXSW_ITEM_H

#include <linux/types.h>
#include <linux/string.h>
#include <linux/bitops.h>

struct mlxsw_item {
	unsigned short	offset;		/* bytes in container */
	short		step;		/* step in bytes for indexed items */
	unsigned short	in_step_offset; /* offset within one step */
	unsigned char	shift;		/* shift in bits */
	unsigned char	element_size;	/* size of element in bit array */
	bool		no_real_shift;
	union {
		unsigned char	bits;
		unsigned short	bytes;
	} size;
	const char	*name;
};

static inline unsigned int
__mlxsw_item_offset(const struct mlxsw_item *item, unsigned short index,
		    size_t typesize)
{
	BUG_ON(index && !item->step);
	if (item->offset % typesize != 0 ||
	    item->step % typesize != 0 ||
	    item->in_step_offset % typesize != 0) {
		pr_err("mlxsw: item bug (name=%s,offset=%x,step=%x,in_step_offset=%x,typesize=%zx)\n",
		       item->name, item->offset, item->step,
		       item->in_step_offset, typesize);
		BUG();
	}

	return ((item->offset + item->step * index + item->in_step_offset) /
		typesize);
}

static inline u8 __mlxsw_item_get8(const char *buf,
				   const struct mlxsw_item *item,
				   unsigned short index)
{
	unsigned int offset = __mlxsw_item_offset(item, index, sizeof(u8));
	u8 *b = (u8 *) buf;
	u8 tmp;

	tmp = b[offset];
	tmp >>= item->shift;
	tmp &= GENMASK(item->size.bits - 1, 0);
	if (item->no_real_shift)
		tmp <<= item->shift;
	return tmp;
}

static inline void __mlxsw_item_set8(char *buf, const struct mlxsw_item *item,
				     unsigned short index, u8 val)
{
	unsigned int offset = __mlxsw_item_offset(item, index,
						  sizeof(u8));
	u8 *b = (u8 *) buf;
	u8 mask = GENMASK(item->size.bits - 1, 0) << item->shift;
	u8 tmp;

	if (!item->no_real_shift)
		val <<= item->shift;
	val &= mask;
	tmp = b[offset];
	tmp &= ~mask;
	tmp |= val;
	b[offset] = tmp;
}

static inline u16 __mlxsw_item_get16(const char *buf,
				     const struct mlxsw_item *item,
				     unsigned short index)
{
	unsigned int offset = __mlxsw_item_offset(item, index, sizeof(u16));
	__be16 *b = (__be16 *) buf;
	u16 tmp;

	tmp = be16_to_cpu(b[offset]);
	tmp >>= item->shift;
	tmp &= GENMASK(item->size.bits - 1, 0);
	if (item->no_real_shift)
		tmp <<= item->shift;
	return tmp;
}

static inline void __mlxsw_item_set16(char *buf, const struct mlxsw_item *item,
				      unsigned short index, u16 val)
{
	unsigned int offset = __mlxsw_item_offset(item, index,
						  sizeof(u16));
	__be16 *b = (__be16 *) buf;
	u16 mask = GENMASK(item->size.bits - 1, 0) << item->shift;
	u16 tmp;

	if (!item->no_real_shift)
		val <<= item->shift;
	val &= mask;
	tmp = be16_to_cpu(b[offset]);
	tmp &= ~mask;
	tmp |= val;
	b[offset] = cpu_to_be16(tmp);
}

static inline u32 __mlxsw_item_get32(const char *buf,
				     const struct mlxsw_item *item,
				     unsigned short index)
{
	unsigned int offset = __mlxsw_item_offset(item, index, sizeof(u32));
	__be32 *b = (__be32 *) buf;
	u32 tmp;

	tmp = be32_to_cpu(b[offset]);
	tmp >>= item->shift;
	tmp &= GENMASK(item->size.bits - 1, 0);
	if (item->no_real_shift)
		tmp <<= item->shift;
	return tmp;
}

static inline void __mlxsw_item_set32(char *buf, const struct mlxsw_item *item,
				      unsigned short index, u32 val)
{
	unsigned int offset = __mlxsw_item_offset(item, index,
						  sizeof(u32));
	__be32 *b = (__be32 *) buf;
	u32 mask = GENMASK(item->size.bits - 1, 0) << item->shift;
	u32 tmp;

	if (!item->no_real_shift)
		val <<= item->shift;
	val &= mask;
	tmp = be32_to_cpu(b[offset]);
	tmp &= ~mask;
	tmp |= val;
	b[offset] = cpu_to_be32(tmp);
}

static inline u64 __mlxsw_item_get64(const char *buf,
				     const struct mlxsw_item *item,
				     unsigned short index)
{
	unsigned int offset = __mlxsw_item_offset(item, index, sizeof(u64));
	__be64 *b = (__be64 *) buf;
	u64 tmp;

	tmp = be64_to_cpu(b[offset]);
	tmp >>= item->shift;
	tmp &= GENMASK_ULL(item->size.bits - 1, 0);
	if (item->no_real_shift)
		tmp <<= item->shift;
	return tmp;
}

static inline void __mlxsw_item_set64(char *buf, const struct mlxsw_item *item,
				      unsigned short index, u64 val)
{
	unsigned int offset = __mlxsw_item_offset(item, index, sizeof(u64));
	__be64 *b = (__be64 *) buf;
	u64 mask = GENMASK_ULL(item->size.bits - 1, 0) << item->shift;
	u64 tmp;

	if (!item->no_real_shift)
		val <<= item->shift;
	val &= mask;
	tmp = be64_to_cpu(b[offset]);
	tmp &= ~mask;
	tmp |= val;
	b[offset] = cpu_to_be64(tmp);
}

static inline void __mlxsw_item_memcpy_from(const char *buf, char *dst,
					    const struct mlxsw_item *item,
					    unsigned short index)
{
	unsigned int offset = __mlxsw_item_offset(item, index, sizeof(char));

	memcpy(dst, &buf[offset], item->size.bytes);
}

static inline void __mlxsw_item_memcpy_to(char *buf, const char *src,
					  const struct mlxsw_item *item,
					  unsigned short index)
{
	unsigned int offset = __mlxsw_item_offset(item, index, sizeof(char));

	memcpy(&buf[offset], src, item->size.bytes);
}

static inline char *__mlxsw_item_data(char *buf, const struct mlxsw_item *item,
				      unsigned short index)
{
	unsigned int offset = __mlxsw_item_offset(item, index, sizeof(char));

	return &buf[offset];
}

static inline u16
__mlxsw_item_bit_array_offset(const struct mlxsw_item *item,
			      u16 index, u8 *shift)
{
	u16 max_index, be_index;
	u16 offset;		/* byte offset inside the array */
	u8 in_byte_index;

	BUG_ON(index && !item->element_size);
	if (item->offset % sizeof(u32) != 0 ||
	    BITS_PER_BYTE % item->element_size != 0) {
		pr_err("mlxsw: item bug (name=%s,offset=%x,element_size=%x)\n",
		       item->name, item->offset, item->element_size);
		BUG();
	}

	max_index = (item->size.bytes << 3) / item->element_size - 1;
	be_index = max_index - index;
	offset = be_index * item->element_size >> 3;
	in_byte_index  = index % (BITS_PER_BYTE / item->element_size);
	*shift = in_byte_index * item->element_size;

	return item->offset + offset;
}

static inline u8 __mlxsw_item_bit_array_get(const char *buf,
					    const struct mlxsw_item *item,
					    u16 index)
{
	u8 shift, tmp;
	u16 offset = __mlxsw_item_bit_array_offset(item, index, &shift);

	tmp = buf[offset];
	tmp >>= shift;
	tmp &= GENMASK(item->element_size - 1, 0);
	return tmp;
}

static inline void __mlxsw_item_bit_array_set(char *buf,
					      const struct mlxsw_item *item,
					      u16 index, u8 val)
{
	u8 shift, tmp;
	u16 offset = __mlxsw_item_bit_array_offset(item, index, &shift);
	u8 mask = GENMASK(item->element_size - 1, 0) << shift;

	val <<= shift;
	val &= mask;
	tmp = buf[offset];
	tmp &= ~mask;
	tmp |= val;
	buf[offset] = tmp;
}

#define __ITEM_NAME(_type, _cname, _iname)					\
	mlxsw_##_type##_##_cname##_##_iname##_item

/* _type: cmd_mbox, reg, etc.
 * _cname: containter name (e.g. command name, register name)
 * _iname: item name within the container
 */

#define MLXSW_ITEM8(_type, _cname, _iname, _offset, _shift, _sizebits)		\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.shift = _shift,							\
	.size = {.bits = _sizebits,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline u8 __maybe_unused							\
mlxsw_##_type##_##_cname##_##_iname##_get(const char *buf)			\
{										\
	return __mlxsw_item_get8(buf, &__ITEM_NAME(_type, _cname, _iname), 0);	\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_set(char *buf, u8 val)			\
{										\
	__mlxsw_item_set8(buf, &__ITEM_NAME(_type, _cname, _iname), 0, val);	\
}

#define MLXSW_ITEM8_INDEXED(_type, _cname, _iname, _offset, _shift, _sizebits,	\
			    _step, _instepoffset, _norealshift)			\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.step = _step,								\
	.in_step_offset = _instepoffset,					\
	.shift = _shift,							\
	.no_real_shift = _norealshift,						\
	.size = {.bits = _sizebits,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline u8 __maybe_unused							\
mlxsw_##_type##_##_cname##_##_iname##_get(const char *buf, unsigned short index)\
{										\
	return __mlxsw_item_get8(buf, &__ITEM_NAME(_type, _cname, _iname),	\
				 index);					\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_set(char *buf, unsigned short index,	\
					  u8 val)				\
{										\
	__mlxsw_item_set8(buf, &__ITEM_NAME(_type, _cname, _iname),		\
			  index, val);						\
}

#define MLXSW_ITEM16(_type, _cname, _iname, _offset, _shift, _sizebits)		\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.shift = _shift,							\
	.size = {.bits = _sizebits,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline u16 __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_get(const char *buf)			\
{										\
	return __mlxsw_item_get16(buf, &__ITEM_NAME(_type, _cname, _iname), 0);	\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_set(char *buf, u16 val)			\
{										\
	__mlxsw_item_set16(buf, &__ITEM_NAME(_type, _cname, _iname), 0, val);	\
}

#define MLXSW_ITEM16_INDEXED(_type, _cname, _iname, _offset, _shift, _sizebits,	\
			     _step, _instepoffset, _norealshift)		\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.step = _step,								\
	.in_step_offset = _instepoffset,					\
	.shift = _shift,							\
	.no_real_shift = _norealshift,						\
	.size = {.bits = _sizebits,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline u16 __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_get(const char *buf, unsigned short index)\
{										\
	return __mlxsw_item_get16(buf, &__ITEM_NAME(_type, _cname, _iname),	\
				  index);					\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_set(char *buf, unsigned short index,	\
					  u16 val)				\
{										\
	__mlxsw_item_set16(buf, &__ITEM_NAME(_type, _cname, _iname),		\
			   index, val);						\
}

#define MLXSW_ITEM32(_type, _cname, _iname, _offset, _shift, _sizebits)		\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.shift = _shift,							\
	.size = {.bits = _sizebits,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline u32 __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_get(const char *buf)			\
{										\
	return __mlxsw_item_get32(buf, &__ITEM_NAME(_type, _cname, _iname), 0);	\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_set(char *buf, u32 val)			\
{										\
	__mlxsw_item_set32(buf, &__ITEM_NAME(_type, _cname, _iname), 0, val);	\
}

#define MLXSW_ITEM32_INDEXED(_type, _cname, _iname, _offset, _shift, _sizebits,	\
			     _step, _instepoffset, _norealshift)		\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.step = _step,								\
	.in_step_offset = _instepoffset,					\
	.shift = _shift,							\
	.no_real_shift = _norealshift,						\
	.size = {.bits = _sizebits,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline u32 __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_get(const char *buf, unsigned short index)\
{										\
	return __mlxsw_item_get32(buf, &__ITEM_NAME(_type, _cname, _iname),	\
				  index);					\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_set(char *buf, unsigned short index,	\
					  u32 val)				\
{										\
	__mlxsw_item_set32(buf, &__ITEM_NAME(_type, _cname, _iname),		\
			   index, val);						\
}

#define MLXSW_ITEM64(_type, _cname, _iname, _offset, _shift, _sizebits)		\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.shift = _shift,							\
	.size = {.bits = _sizebits,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline u64 __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_get(const char *buf)			\
{										\
	return __mlxsw_item_get64(buf, &__ITEM_NAME(_type, _cname, _iname), 0);	\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_set(char *buf, u64 val)			\
{										\
	__mlxsw_item_set64(buf, &__ITEM_NAME(_type, _cname, _iname), 0,	val);	\
}

#define MLXSW_ITEM64_INDEXED(_type, _cname, _iname, _offset, _shift,		\
			     _sizebits, _step, _instepoffset, _norealshift)	\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.step = _step,								\
	.in_step_offset = _instepoffset,					\
	.shift = _shift,							\
	.no_real_shift = _norealshift,						\
	.size = {.bits = _sizebits,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline u64 __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_get(const char *buf, unsigned short index)\
{										\
	return __mlxsw_item_get64(buf, &__ITEM_NAME(_type, _cname, _iname),	\
				  index);					\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_set(char *buf, unsigned short index,	\
					  u64 val)				\
{										\
	__mlxsw_item_set64(buf, &__ITEM_NAME(_type, _cname, _iname),		\
			   index, val);						\
}

#define MLXSW_ITEM_BUF(_type, _cname, _iname, _offset, _sizebytes)		\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.size = {.bytes = _sizebytes,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_memcpy_from(const char *buf, char *dst)	\
{										\
	__mlxsw_item_memcpy_from(buf, dst,					\
				 &__ITEM_NAME(_type, _cname, _iname), 0);	\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_memcpy_to(char *buf, const char *src)	\
{										\
	__mlxsw_item_memcpy_to(buf, src,					\
			       &__ITEM_NAME(_type, _cname, _iname), 0);		\
}										\
static inline char * __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_data(char *buf)				\
{										\
	return __mlxsw_item_data(buf, &__ITEM_NAME(_type, _cname, _iname), 0);	\
}

#define MLXSW_ITEM_BUF_INDEXED(_type, _cname, _iname, _offset, _sizebytes,	\
			       _step, _instepoffset)				\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.step = _step,								\
	.in_step_offset = _instepoffset,					\
	.size = {.bytes = _sizebytes,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_memcpy_from(const char *buf,		\
						  unsigned short index,		\
						  char *dst)			\
{										\
	__mlxsw_item_memcpy_from(buf, dst,					\
				 &__ITEM_NAME(_type, _cname, _iname), index);	\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_memcpy_to(char *buf,			\
						unsigned short index,		\
						const char *src)		\
{										\
	__mlxsw_item_memcpy_to(buf, src,					\
			       &__ITEM_NAME(_type, _cname, _iname), index);	\
}										\
static inline char * __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_data(char *buf, unsigned short index)	\
{										\
	return __mlxsw_item_data(buf,						\
				 &__ITEM_NAME(_type, _cname, _iname), index);	\
}

#define MLXSW_ITEM_BIT_ARRAY(_type, _cname, _iname, _offset, _sizebytes,	\
			     _element_size)					\
static struct mlxsw_item __ITEM_NAME(_type, _cname, _iname) = {			\
	.offset = _offset,							\
	.element_size = _element_size,						\
	.size = {.bytes = _sizebytes,},						\
	.name = #_type "_" #_cname "_" #_iname,					\
};										\
static inline u8 __maybe_unused							\
mlxsw_##_type##_##_cname##_##_iname##_get(const char *buf, u16 index)		\
{										\
	return __mlxsw_item_bit_array_get(buf,					\
					  &__ITEM_NAME(_type, _cname, _iname),	\
					  index);				\
}										\
static inline void __maybe_unused						\
mlxsw_##_type##_##_cname##_##_iname##_set(char *buf, u16 index, u8 val)		\
{										\
	return __mlxsw_item_bit_array_set(buf,					\
					  &__ITEM_NAME(_type, _cname, _iname),	\
					  index, val);				\
}										\

#endif
