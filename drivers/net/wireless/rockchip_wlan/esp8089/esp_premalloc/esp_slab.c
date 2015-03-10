/* Copyright (c) 2014 - 2015 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 */
#ifdef ESP_SLAB

#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include "esp_slab.h"
#include "esp_log.h"

static void  *gl_mem_p;
static struct esp_mem_mgmt gl_mm;

int set_gl_mem_p(void *mem_p)
{
	gl_mem_p = mem_p;

	return 0;
}

void *get_gl_mem_p(void)
{
	return gl_mem_p;
}

static inline int esp_clz(u32 x)
{
	if (x == 0x0)
		return 32;
	else
		return __builtin_clz(x);
}

static inline int esp_ctz(u32 x)
{
	if (x == 0x0)
		return 32;
	else
		return __builtin_ctz(x);
}

static inline int esp_popcount(u32 x)
{
	int i;

	i = 0;
	while (x) {
		if (x&0x1)
			i++;
		x = (x>>1);
	}
	return i;
}

/* bit_no value must be 0 */
static int get_next_empty_num(u32* bit_map, int bit_map_size, int bit_no)
{
	int i, x, offset;
	int sum;

	i = (bit_no&0xffffffe0)>>5;
	offset = (bit_no&0x0000001f);

	x = esp_ctz(bit_map[i] >> offset);
	if (x < 32 - offset) {
		return x;
	} else {
		sum = 32 - offset;
		i++;
	}

	while (i < bit_map_size) {
		x = esp_ctz(bit_map[i]);
		if (x < 32) {
			sum += x;
			break;
		} else {
			sum += x;
		}
		i++;
	}

	if (i == bit_map_size)
		return -ERANGE;

	return sum;
}

/* this inline for ignore warning of unused */
static inline int get_prev_empty_num(u32* bit_map, int bit_map_size, int bit_no)
{
	int i, x, offset;
	int sum;

	i = (bit_no&0xffffffe0)>>5;
	offset = (bit_no&0x0000001f);

	x = esp_clz(bit_map[i] << (32 - offset));
	if (x < offset) {
		return x;
	} else {
		sum = offset;
		i--;
	}

	while (i >= 0) {
		x = esp_clz(bit_map[i]);
		if (x < 32) {
			sum += x;
			break;
		} else {
			sum += x;
		}
		i--;
	}

	if (i == bit_map_size)
		return -ERANGE;

	return sum;
}


static int find_1st_empty_pos(u32* bit_map, int bit_map_size, int start_bit_no)
{
	int i, x, offset;
	int pos;

	i = (start_bit_no&0xffffffe0)>>5;  /* integer div 32 */
	offset = (start_bit_no&0x0000001f); /* mod 32 */

	if (esp_popcount((u32)(bit_map[i]>>offset)) < 32 - offset) {
		while ((x = esp_ctz(bit_map[i]>>offset)) == 0) {
			offset++;
		}
		return (i<<5) + offset;
	} else {
		i++;
		pos = i<<5;
	}
	
	while (i < bit_map_size) {
		if (esp_popcount(bit_map[i]) < 32) {
			offset = 0;
			while ((x = esp_ctz(bit_map[i]>>offset)) == 0) {
				offset++;
			}
			pos += offset;
			break;
		} else {
			i++;
			pos = i<<5;
		}
	}

	if (i == bit_map_size)
		return -ERANGE;

	return pos;
}

/* start_bit must be 0 */
static void set_next_full(u32 *bit_map, int bit_map_size, int start_bit_no, int bit_count)
{
	int i, offset;

	i = (start_bit_no&0xffffffe0)>>5;  /* integer div 32 */
	offset = (start_bit_no&0x0000001f); /* mod 32 */

	if (bit_count > 32 - offset) {
		bit_map[i] |= ~((1<<offset) - 1);
		bit_count = bit_count - (32 - offset);
		while (bit_count > 0) {
			i++;
			if (bit_count >= 32) {
				bit_map[i] |= 0xffffffff;
			} else {
				bit_map[i] |= ((1<<bit_count) -1);
			}
			bit_count -= 32;
		}
	} else {
		bit_map[i] |= (((1<<bit_count) - 1) << offset);
	}
}

/* start_bit must be 1 */
static void set_next_empty(u32 *bit_map, int bit_map_size, int start_bit_no, int bit_count)
{
	int i, offset;

	i = (start_bit_no&0xffffffe0)>>5;  /* integer div 32 */
	offset = (start_bit_no&0x0000001f); /* mod 32 */

	if (bit_count > 32 - offset) {
		bit_map[i] &= ((1<<offset) - 1);
		bit_count = bit_count - (32 - offset);
		while (bit_count > 0) {
			i++;
			if (bit_count >= 32) {
				bit_map[i] &= 0x00000000;
			} else {
				bit_map[i] &= ~((1<<bit_count) -1);
			}
			bit_count -= 32;
		}
	} else {
		bit_map[i] &= ~(((1<<bit_count) - 1) << offset);
	}
}



static inline int bin_roundup(int n, unsigned int div) /*div must be 2^n */
{
	int x;

	x = esp_ctz(div);

	if (esp_popcount(div) > 1)
		return -EINVAL;
	
	return  (n & ((1<<x)-1) ? ((n>>x) + 1)<<x : n);
}


/* must use hash to make it faster, when have time*/
static int set_access(struct esp_mem_mgmt_per *mmp, void *point, int bit_count)
{
	int i;

	if (!mmp)
		return -EINVAL;

	for (i = 0; i < DEFAULT_MAX_ACCESSES_PER; i++) {
		if (mmp->macs[i].p == NULL) {
			mmp->macs[i].p = point;
			mmp->macs[i].pieces = bit_count;
			break;
		}
	}

	if (i == DEFAULT_MAX_ACCESSES_PER)
		return -ERANGE;
	return 0;
}

static int get_clear_access(struct esp_mem_mgmt_per *mmp, void *point)
{
	int pieces, i;

	if (!mmp)
		return -EINVAL;

	for (i = 0; i < DEFAULT_MAX_ACCESSES_PER; i++) {
		if (mmp->macs[i].p == point) {
			pieces = mmp->macs[i].pieces;
			mmp->macs[i].p = NULL;
			mmp->macs[i].pieces = 0;
			return pieces;
		}
	}

	return -ERANGE;
}

static inline void *_esp_malloc(size_t size)
{
	int pos;
	int empty_num;
	int roundup_size;
	void *p;

	if (size >= LITTLE_LIMIT) {                        /* use large mem */
		spin_lock_irqsave(&gl_mm.large_mmp.spin_lock, gl_mm.large_mmp.lock_flags);
		pos = 0;
		while (pos < LARGE_PIECES_NUM) {
			if ((pos = find_1st_empty_pos(gl_mm.large_mmp.bit_map, LARGE_BIT_MAP_SIZE, pos)) >= 0) {
				logd("large find pos [%d]\n", pos);
				empty_num = get_next_empty_num(gl_mm.large_mmp.bit_map, LARGE_BIT_MAP_SIZE, pos);
				if ((empty_num<<LARGE_PIECE_SIZE_SHIFT) >= size) {
					roundup_size = bin_roundup(size, LARGE_PIECE_SIZE);
					logd("large roundup_size [%d], pieces [%d]\n", roundup_size, roundup_size>>LARGE_PIECE_SIZE_SHIFT);
					p = (void *)((pos<<LARGE_PIECE_SIZE_SHIFT) + (u8 *)gl_mm.large_mmp.start_p);
					if (set_access(&gl_mm.large_mmp, p, roundup_size>>LARGE_PIECE_SIZE_SHIFT) != 0) {
						spin_unlock_irqrestore(&gl_mm.large_mmp.spin_lock, gl_mm.large_mmp.lock_flags);
						return NULL;
					}
					set_next_full(gl_mm.large_mmp.bit_map, LARGE_BIT_MAP_SIZE, pos, roundup_size>>LARGE_PIECE_SIZE_SHIFT);
					spin_unlock_irqrestore(&gl_mm.large_mmp.spin_lock, gl_mm.large_mmp.lock_flags);
					return p;
				} else {
					pos += empty_num;
				}
			} else {                           /* no enough memory */
				spin_unlock_irqrestore(&gl_mm.large_mmp.spin_lock, gl_mm.large_mmp.lock_flags);
				return NULL;
			}
		}
		spin_unlock_irqrestore(&gl_mm.large_mmp.spin_lock, gl_mm.large_mmp.lock_flags);

	} else {                                           /* use little mem */
		spin_lock_irqsave(&gl_mm.little_mmp.spin_lock, gl_mm.little_mmp.lock_flags);
		pos = 0;
		while (pos < LITTLE_PIECES_NUM) {
			if ((pos = find_1st_empty_pos(gl_mm.little_mmp.bit_map, LITTLE_BIT_MAP_SIZE, pos)) >= 0) {
				logd("little find pos [%d]\n", pos);
				empty_num = get_next_empty_num(gl_mm.little_mmp.bit_map, LITTLE_BIT_MAP_SIZE, pos);
				if ((empty_num<<LITTLE_PIECE_SIZE_SHIFT) >= size) {
					roundup_size = bin_roundup(size, LITTLE_PIECE_SIZE);
					logd("little roundup_size [%d], pieces [%d]\n", roundup_size, roundup_size>>LITTLE_PIECE_SIZE_SHIFT);
					p = (void *)((pos<<LITTLE_PIECE_SIZE_SHIFT) + (u8 *)gl_mm.little_mmp.start_p);
					if (set_access(&gl_mm.little_mmp, p, roundup_size>>LITTLE_PIECE_SIZE_SHIFT) != 0) {
						spin_unlock_irqrestore(&gl_mm.little_mmp.spin_lock, gl_mm.little_mmp.lock_flags);
						return NULL;
					}
					set_next_full(gl_mm.little_mmp.bit_map, LITTLE_BIT_MAP_SIZE, pos, roundup_size>>LITTLE_PIECE_SIZE_SHIFT);
					spin_unlock_irqrestore(&gl_mm.little_mmp.spin_lock, gl_mm.little_mmp.lock_flags);
					return p;
				} else {
					pos += empty_num;
				}
			} else {                           /* no enough memory */
				spin_unlock_irqrestore(&gl_mm.little_mmp.spin_lock, gl_mm.little_mmp.lock_flags);
				return NULL;
			}
		}
		spin_unlock_irqrestore(&gl_mm.little_mmp.spin_lock, gl_mm.little_mmp.lock_flags);
	}

	return NULL;
}

static inline void _esp_free(void *p)
{
	struct esp_mem_mgmt_per *mmp;
	int pieces;
	int start_pos;


	mmp = &gl_mm.large_mmp;
	spin_lock_irqsave(&mmp->spin_lock, mmp->lock_flags);
	pieces = get_clear_access(&gl_mm.large_mmp, p);
	logd("%s large pieces %d\n", __func__, pieces);
	if (pieces > 0) {
		start_pos = ((p - mmp->start_p)>>LARGE_PIECE_SIZE_SHIFT);
		logd("%s large pos %d\n", __func__, start_pos);
		set_next_empty(mmp->bit_map, LARGE_BIT_MAP_SIZE, start_pos, pieces);
		spin_unlock_irqrestore(&mmp->spin_lock, mmp->lock_flags);
		return;
	} else
		spin_unlock_irqrestore(&mmp->spin_lock, mmp->lock_flags);

	mmp = &gl_mm.little_mmp;
	spin_lock_irqsave(&mmp->spin_lock, mmp->lock_flags);
	pieces = get_clear_access(&gl_mm.little_mmp, p);
	logd(KERN_ERR "%s little pieces %d\n", __func__, pieces);
	if (pieces > 0) {
		start_pos = ((p - mmp->start_p)>>LITTLE_PIECE_SIZE_SHIFT);
		logd(KERN_ERR "%s little pos %d\n", __func__, start_pos);
		set_next_empty(mmp->bit_map, LITTLE_BIT_MAP_SIZE, start_pos, pieces);
		spin_unlock_irqrestore(&mmp->spin_lock, mmp->lock_flags);
		return;
	} else
		spin_unlock_irqrestore(&mmp->spin_lock, mmp->lock_flags);

	loge(KERN_ERR "%s point is not alloc from esp prealloc program\n", __func__);

	return;
}

#ifdef ESP_DEBUG
void show_bitmap(void)
{
	int i;

	logi("-----LARGE BIT MAP-----\n");
	for (i = 0; i < LARGE_BIT_MAP_SIZE; i++) {
		logi("%04d: 0x%08x\n", i, gl_mm.large_mmp.bit_map[i]);
	}
	
	logi("-----LITTLE BIT MAP-----\n");
	for (i = 0; i < LITTLE_BIT_MAP_SIZE; i++) {
		logi("%04d: 0x%08x\n", i, gl_mm.little_mmp.bit_map[i]);
	}
}
#endif

void *esp_malloc(size_t size)
{
#ifdef ESP_DEBUG
	void *p = _esp_malloc(size);
	show_bitmap();
	return p;
#else
	return _esp_malloc(size);
#endif

}
EXPORT_SYMBOL(esp_malloc);

void esp_free(void *p)
{
	_esp_free(p);
#ifdef ESP_DEBUG
	show_bitmap();
#endif
}
EXPORT_SYMBOL(esp_free);


void *esp_pre_malloc(void)
{
	int po;

	po = get_order(PREMALLOC_SIZE);
        gl_mem_p = (void *)__get_free_pages(GFP_ATOMIC, po);

        if (gl_mem_p == NULL) {
                loge("%s no mem for premalloc! \n", __func__);
                return NULL;
	}

	return gl_mem_p;
}

void esp_pre_free(void)
{
	int po;

        if (gl_mem_p == NULL) {
                loge("%s no mem for prefree! \n", __func__);
                return;
	}

	po = get_order(PREMALLOC_SIZE);
	free_pages((unsigned long)gl_mem_p, po);
	gl_mem_p = NULL;
}

int esp_mm_init(void)
{
	memset(&gl_mm, 0x00, sizeof(struct esp_mem_mgmt));

	spin_lock_init(&gl_mm.large_mmp.spin_lock);
	spin_lock_init(&gl_mm.little_mmp.spin_lock);

	gl_mm.large_mmp.start_p = gl_mem_p; /* large */
	gl_mm.little_mmp.start_p = gl_mem_p + PREMALLOC_LARGE_SIZE; /* large */

	if ((gl_mm.large_mmp.bit_map = (u32*)kzalloc(sizeof(u32)*LARGE_BIT_MAP_SIZE, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	if ((gl_mm.little_mmp.bit_map = (u32*)kzalloc(sizeof(u32)*LITTLE_BIT_MAP_SIZE, GFP_KERNEL)) == NULL) {
		kfree(gl_mm.large_mmp.bit_map);
		return -ENOMEM;
	}

#ifdef ESP_DEBUG
	show_bitmap();
#endif
	return 0;
}

void esp_mm_deinit(void)
{
	if (gl_mm.large_mmp.bit_map)
		kfree(gl_mm.large_mmp.bit_map);

	if (gl_mm.little_mmp.bit_map)
		kfree(gl_mm.little_mmp.bit_map);
}

int esp_slab_init(void)
{
	int err = 0;
	if (esp_pre_malloc() ==  NULL) {
		err = -ENOMEM;
		return err;
	}

	err = esp_mm_init();
		
	return err;
}

void esp_slab_deinit(void)
{
	esp_pre_free();
	esp_mm_init();
}

#endif /* ESP_SLAB */
