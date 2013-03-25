/*
 * random utiility code, for bcache but in theory not specific to bcache
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#include "util.h"

#define simple_strtoint(c, end, base)	simple_strtol(c, end, base)
#define simple_strtouint(c, end, base)	simple_strtoul(c, end, base)

#define STRTO_H(name, type)					\
int name ## _h(const char *cp, type *res)		        \
{								\
	int u = 0;						\
	char *e;						\
	type i = simple_ ## name(cp, &e, 10);			\
								\
	switch (tolower(*e)) {					\
	default:						\
		return -EINVAL;					\
	case 'y':						\
	case 'z':						\
		u++;						\
	case 'e':						\
		u++;						\
	case 'p':						\
		u++;						\
	case 't':						\
		u++;						\
	case 'g':						\
		u++;						\
	case 'm':						\
		u++;						\
	case 'k':						\
		u++;						\
		if (e++ == cp)					\
			return -EINVAL;				\
	case '\n':						\
	case '\0':						\
		if (*e == '\n')					\
			e++;					\
	}							\
								\
	if (*e)							\
		return -EINVAL;					\
								\
	while (u--) {						\
		if ((type) ~0 > 0 &&				\
		    (type) ~0 / 1024 <= i)			\
			return -EINVAL;				\
		if ((i > 0 && ANYSINT_MAX(type) / 1024 < i) ||	\
		    (i < 0 && -ANYSINT_MAX(type) / 1024 > i))	\
			return -EINVAL;				\
		i *= 1024;					\
	}							\
								\
	*res = i;						\
	return 0;						\
}								\
EXPORT_SYMBOL_GPL(name ## _h);

STRTO_H(strtoint, int)
STRTO_H(strtouint, unsigned int)
STRTO_H(strtoll, long long)
STRTO_H(strtoull, unsigned long long)

ssize_t hprint(char *buf, int64_t v)
{
	static const char units[] = "?kMGTPEZY";
	char dec[3] = "";
	int u, t = 0;

	for (u = 0; v >= 1024 || v <= -1024; u++) {
		t = v & ~(~0 << 10);
		v >>= 10;
	}

	if (!u)
		return sprintf(buf, "%llu", v);

	if (v < 100 && v > -100)
		sprintf(dec, ".%i", t / 100);

	return sprintf(buf, "%lli%s%c", v, dec, units[u]);
}
EXPORT_SYMBOL_GPL(hprint);

ssize_t snprint_string_list(char *buf, size_t size, const char * const list[],
			    size_t selected)
{
	char *out = buf;
	size_t i;

	for (i = 0; list[i]; i++)
		out += snprintf(out, buf + size - out,
				i == selected ? "[%s] " : "%s ", list[i]);

	out[-1] = '\n';
	return out - buf;
}
EXPORT_SYMBOL_GPL(snprint_string_list);

ssize_t read_string_list(const char *buf, const char * const list[])
{
	size_t i;
	char *s, *d = kstrndup(buf, PAGE_SIZE - 1, GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	s = strim(d);

	for (i = 0; list[i]; i++)
		if (!strcmp(list[i], s))
			break;

	kfree(d);

	if (!list[i])
		return -EINVAL;

	return i;
}
EXPORT_SYMBOL_GPL(read_string_list);

bool is_zero(const char *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (p[i])
			return false;
	return true;
}
EXPORT_SYMBOL_GPL(is_zero);

int parse_uuid(const char *s, char *uuid)
{
	size_t i, j, x;
	memset(uuid, 0, 16);

	for (i = 0, j = 0;
	     i < strspn(s, "-0123456789:ABCDEFabcdef") && j < 32;
	     i++) {
		x = s[i] | 32;

		switch (x) {
		case '0'...'9':
			x -= '0';
			break;
		case 'a'...'f':
			x -= 'a' - 10;
			break;
		default:
			continue;
		}

		if (!(j & 1))
			x <<= 4;
		uuid[j++ >> 1] |= x;
	}
	return i;
}
EXPORT_SYMBOL_GPL(parse_uuid);

void time_stats_update(struct time_stats *stats, uint64_t start_time)
{
	uint64_t now		= local_clock();
	uint64_t duration	= time_after64(now, start_time)
		? now - start_time : 0;
	uint64_t last		= time_after64(now, stats->last)
		? now - stats->last : 0;

	stats->max_duration = max(stats->max_duration, duration);

	if (stats->last) {
		ewma_add(stats->average_duration, duration, 8, 8);

		if (stats->average_frequency)
			ewma_add(stats->average_frequency, last, 8, 8);
		else
			stats->average_frequency  = last << 8;
	} else {
		stats->average_duration  = duration << 8;
	}

	stats->last = now ?: 1;
}
EXPORT_SYMBOL_GPL(time_stats_update);

unsigned next_delay(struct ratelimit *d, uint64_t done)
{
	uint64_t now = local_clock();

	d->next += div_u64(done, d->rate);

	return time_after64(d->next, now)
		? div_u64(d->next - now, NSEC_PER_SEC / HZ)
		: 0;
}
EXPORT_SYMBOL_GPL(next_delay);

void bio_map(struct bio *bio, void *base)
{
	size_t size = bio->bi_size;
	struct bio_vec *bv = bio->bi_io_vec;

	BUG_ON(!bio->bi_size);
	BUG_ON(bio->bi_vcnt);

	bv->bv_offset = base ? ((unsigned long) base) % PAGE_SIZE : 0;
	goto start;

	for (; size; bio->bi_vcnt++, bv++) {
		bv->bv_offset	= 0;
start:		bv->bv_len	= min_t(size_t, PAGE_SIZE - bv->bv_offset,
					size);
		if (base) {
			bv->bv_page = is_vmalloc_addr(base)
				? vmalloc_to_page(base)
				: virt_to_page(base);

			base += bv->bv_len;
		}

		size -= bv->bv_len;
	}
}
EXPORT_SYMBOL_GPL(bio_map);

int bio_alloc_pages(struct bio *bio, gfp_t gfp)
{
	int i;
	struct bio_vec *bv;

	bio_for_each_segment(bv, bio, i) {
		bv->bv_page = alloc_page(gfp);
		if (!bv->bv_page) {
			while (bv-- != bio->bi_io_vec + bio->bi_idx)
				__free_page(bv->bv_page);
			return -ENOMEM;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(bio_alloc_pages);

/*
 * Portions Copyright (c) 1996-2001, PostgreSQL Global Development Group (Any
 * use permitted, subject to terms of PostgreSQL license; see.)

 * If we have a 64-bit integer type, then a 64-bit CRC looks just like the
 * usual sort of implementation. (See Ross Williams' excellent introduction
 * A PAINLESS GUIDE TO CRC ERROR DETECTION ALGORITHMS, available from
 * ftp://ftp.rocksoft.com/papers/crc_v3.txt or several other net sites.)
 * If we have no working 64-bit type, then fake it with two 32-bit registers.
 *
 * The present implementation is a normal (not "reflected", in Williams'
 * terms) 64-bit CRC, using initial all-ones register contents and a final
 * bit inversion. The chosen polynomial is borrowed from the DLT1 spec
 * (ECMA-182, available from http://www.ecma.ch/ecma1/STAND/ECMA-182.HTM):
 *
 * x^64 + x^62 + x^57 + x^55 + x^54 + x^53 + x^52 + x^47 + x^46 + x^45 +
 * x^40 + x^39 + x^38 + x^37 + x^35 + x^33 + x^32 + x^31 + x^29 + x^27 +
 * x^24 + x^23 + x^22 + x^21 + x^19 + x^17 + x^13 + x^12 + x^10 + x^9 +
 * x^7 + x^4 + x + 1
*/

static const uint64_t crc_table[256] = {
	0x0000000000000000, 0x42F0E1EBA9EA3693, 0x85E1C3D753D46D26,
	0xC711223CFA3E5BB5, 0x493366450E42ECDF, 0x0BC387AEA7A8DA4C,
	0xCCD2A5925D9681F9, 0x8E224479F47CB76A, 0x9266CC8A1C85D9BE,
	0xD0962D61B56FEF2D, 0x17870F5D4F51B498, 0x5577EEB6E6BB820B,
	0xDB55AACF12C73561, 0x99A54B24BB2D03F2, 0x5EB4691841135847,
	0x1C4488F3E8F96ED4, 0x663D78FF90E185EF, 0x24CD9914390BB37C,
	0xE3DCBB28C335E8C9, 0xA12C5AC36ADFDE5A, 0x2F0E1EBA9EA36930,
	0x6DFEFF5137495FA3, 0xAAEFDD6DCD770416, 0xE81F3C86649D3285,
	0xF45BB4758C645C51, 0xB6AB559E258E6AC2, 0x71BA77A2DFB03177,
	0x334A9649765A07E4, 0xBD68D2308226B08E, 0xFF9833DB2BCC861D,
	0x388911E7D1F2DDA8, 0x7A79F00C7818EB3B, 0xCC7AF1FF21C30BDE,
	0x8E8A101488293D4D, 0x499B3228721766F8, 0x0B6BD3C3DBFD506B,
	0x854997BA2F81E701, 0xC7B97651866BD192, 0x00A8546D7C558A27,
	0x4258B586D5BFBCB4, 0x5E1C3D753D46D260, 0x1CECDC9E94ACE4F3,
	0xDBFDFEA26E92BF46, 0x990D1F49C77889D5, 0x172F5B3033043EBF,
	0x55DFBADB9AEE082C, 0x92CE98E760D05399, 0xD03E790CC93A650A,
	0xAA478900B1228E31, 0xE8B768EB18C8B8A2, 0x2FA64AD7E2F6E317,
	0x6D56AB3C4B1CD584, 0xE374EF45BF6062EE, 0xA1840EAE168A547D,
	0x66952C92ECB40FC8, 0x2465CD79455E395B, 0x3821458AADA7578F,
	0x7AD1A461044D611C, 0xBDC0865DFE733AA9, 0xFF3067B657990C3A,
	0x711223CFA3E5BB50, 0x33E2C2240A0F8DC3, 0xF4F3E018F031D676,
	0xB60301F359DBE0E5, 0xDA050215EA6C212F, 0x98F5E3FE438617BC,
	0x5FE4C1C2B9B84C09, 0x1D14202910527A9A, 0x93366450E42ECDF0,
	0xD1C685BB4DC4FB63, 0x16D7A787B7FAA0D6, 0x5427466C1E109645,
	0x4863CE9FF6E9F891, 0x0A932F745F03CE02, 0xCD820D48A53D95B7,
	0x8F72ECA30CD7A324, 0x0150A8DAF8AB144E, 0x43A04931514122DD,
	0x84B16B0DAB7F7968, 0xC6418AE602954FFB, 0xBC387AEA7A8DA4C0,
	0xFEC89B01D3679253, 0x39D9B93D2959C9E6, 0x7B2958D680B3FF75,
	0xF50B1CAF74CF481F, 0xB7FBFD44DD257E8C, 0x70EADF78271B2539,
	0x321A3E938EF113AA, 0x2E5EB66066087D7E, 0x6CAE578BCFE24BED,
	0xABBF75B735DC1058, 0xE94F945C9C3626CB, 0x676DD025684A91A1,
	0x259D31CEC1A0A732, 0xE28C13F23B9EFC87, 0xA07CF2199274CA14,
	0x167FF3EACBAF2AF1, 0x548F120162451C62, 0x939E303D987B47D7,
	0xD16ED1D631917144, 0x5F4C95AFC5EDC62E, 0x1DBC74446C07F0BD,
	0xDAAD56789639AB08, 0x985DB7933FD39D9B, 0x84193F60D72AF34F,
	0xC6E9DE8B7EC0C5DC, 0x01F8FCB784FE9E69, 0x43081D5C2D14A8FA,
	0xCD2A5925D9681F90, 0x8FDAB8CE70822903, 0x48CB9AF28ABC72B6,
	0x0A3B7B1923564425, 0x70428B155B4EAF1E, 0x32B26AFEF2A4998D,
	0xF5A348C2089AC238, 0xB753A929A170F4AB, 0x3971ED50550C43C1,
	0x7B810CBBFCE67552, 0xBC902E8706D82EE7, 0xFE60CF6CAF321874,
	0xE224479F47CB76A0, 0xA0D4A674EE214033, 0x67C58448141F1B86,
	0x253565A3BDF52D15, 0xAB1721DA49899A7F, 0xE9E7C031E063ACEC,
	0x2EF6E20D1A5DF759, 0x6C0603E6B3B7C1CA, 0xF6FAE5C07D3274CD,
	0xB40A042BD4D8425E, 0x731B26172EE619EB, 0x31EBC7FC870C2F78,
	0xBFC9838573709812, 0xFD39626EDA9AAE81, 0x3A28405220A4F534,
	0x78D8A1B9894EC3A7, 0x649C294A61B7AD73, 0x266CC8A1C85D9BE0,
	0xE17DEA9D3263C055, 0xA38D0B769B89F6C6, 0x2DAF4F0F6FF541AC,
	0x6F5FAEE4C61F773F, 0xA84E8CD83C212C8A, 0xEABE6D3395CB1A19,
	0x90C79D3FEDD3F122, 0xD2377CD44439C7B1, 0x15265EE8BE079C04,
	0x57D6BF0317EDAA97, 0xD9F4FB7AE3911DFD, 0x9B041A914A7B2B6E,
	0x5C1538ADB04570DB, 0x1EE5D94619AF4648, 0x02A151B5F156289C,
	0x4051B05E58BC1E0F, 0x87409262A28245BA, 0xC5B073890B687329,
	0x4B9237F0FF14C443, 0x0962D61B56FEF2D0, 0xCE73F427ACC0A965,
	0x8C8315CC052A9FF6, 0x3A80143F5CF17F13, 0x7870F5D4F51B4980,
	0xBF61D7E80F251235, 0xFD913603A6CF24A6, 0x73B3727A52B393CC,
	0x31439391FB59A55F, 0xF652B1AD0167FEEA, 0xB4A25046A88DC879,
	0xA8E6D8B54074A6AD, 0xEA16395EE99E903E, 0x2D071B6213A0CB8B,
	0x6FF7FA89BA4AFD18, 0xE1D5BEF04E364A72, 0xA3255F1BE7DC7CE1,
	0x64347D271DE22754, 0x26C49CCCB40811C7, 0x5CBD6CC0CC10FAFC,
	0x1E4D8D2B65FACC6F, 0xD95CAF179FC497DA, 0x9BAC4EFC362EA149,
	0x158E0A85C2521623, 0x577EEB6E6BB820B0, 0x906FC95291867B05,
	0xD29F28B9386C4D96, 0xCEDBA04AD0952342, 0x8C2B41A1797F15D1,
	0x4B3A639D83414E64, 0x09CA82762AAB78F7, 0x87E8C60FDED7CF9D,
	0xC51827E4773DF90E, 0x020905D88D03A2BB, 0x40F9E43324E99428,
	0x2CFFE7D5975E55E2, 0x6E0F063E3EB46371, 0xA91E2402C48A38C4,
	0xEBEEC5E96D600E57, 0x65CC8190991CB93D, 0x273C607B30F68FAE,
	0xE02D4247CAC8D41B, 0xA2DDA3AC6322E288, 0xBE992B5F8BDB8C5C,
	0xFC69CAB42231BACF, 0x3B78E888D80FE17A, 0x7988096371E5D7E9,
	0xF7AA4D1A85996083, 0xB55AACF12C735610, 0x724B8ECDD64D0DA5,
	0x30BB6F267FA73B36, 0x4AC29F2A07BFD00D, 0x08327EC1AE55E69E,
	0xCF235CFD546BBD2B, 0x8DD3BD16FD818BB8, 0x03F1F96F09FD3CD2,
	0x41011884A0170A41, 0x86103AB85A2951F4, 0xC4E0DB53F3C36767,
	0xD8A453A01B3A09B3, 0x9A54B24BB2D03F20, 0x5D45907748EE6495,
	0x1FB5719CE1045206, 0x919735E51578E56C, 0xD367D40EBC92D3FF,
	0x1476F63246AC884A, 0x568617D9EF46BED9, 0xE085162AB69D5E3C,
	0xA275F7C11F7768AF, 0x6564D5FDE549331A, 0x279434164CA30589,
	0xA9B6706FB8DFB2E3, 0xEB46918411358470, 0x2C57B3B8EB0BDFC5,
	0x6EA7525342E1E956, 0x72E3DAA0AA188782, 0x30133B4B03F2B111,
	0xF7021977F9CCEAA4, 0xB5F2F89C5026DC37, 0x3BD0BCE5A45A6B5D,
	0x79205D0E0DB05DCE, 0xBE317F32F78E067B, 0xFCC19ED95E6430E8,
	0x86B86ED5267CDBD3, 0xC4488F3E8F96ED40, 0x0359AD0275A8B6F5,
	0x41A94CE9DC428066, 0xCF8B0890283E370C, 0x8D7BE97B81D4019F,
	0x4A6ACB477BEA5A2A, 0x089A2AACD2006CB9, 0x14DEA25F3AF9026D,
	0x562E43B4931334FE, 0x913F6188692D6F4B, 0xD3CF8063C0C759D8,
	0x5DEDC41A34BBEEB2, 0x1F1D25F19D51D821, 0xD80C07CD676F8394,
	0x9AFCE626CE85B507
};

uint64_t crc64_update(uint64_t crc, const void *_data, size_t len)
{
	const unsigned char *data = _data;

	while (len--) {
		int i = ((int) (crc >> 56) ^ *data++) & 0xFF;
		crc = crc_table[i] ^ (crc << 8);
	}

	return crc;
}
EXPORT_SYMBOL(crc64_update);

uint64_t crc64(const void *data, size_t len)
{
	uint64_t crc = 0xffffffffffffffff;

	crc = crc64_update(crc, data, len);

	return crc ^ 0xffffffffffffffff;
}
EXPORT_SYMBOL(crc64);
