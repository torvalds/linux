// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>

#define CSKY_PMU_MAX_EVENTS 32
#define DEFAULT_COUNT_WIDTH 48

#define HPCR		"<0, 0x0>"      /* PMU Control reg */
#define HPSPR		"<0, 0x1>"      /* Start PC reg */
#define HPEPR		"<0, 0x2>"      /* End PC reg */
#define HPSIR		"<0, 0x3>"      /* Soft Counter reg */
#define HPCNTENR	"<0, 0x4>"      /* Count Enable reg */
#define HPINTENR	"<0, 0x5>"      /* Interrupt Enable reg */
#define HPOFSR		"<0, 0x6>"      /* Interrupt Status reg */

/* The events for a given PMU register set. */
struct pmu_hw_events {
	/*
	 * The events that are active on the PMU for the given index.
	 */
	struct perf_event *events[CSKY_PMU_MAX_EVENTS];

	/*
	 * A 1 bit for an index indicates that the counter is being used for
	 * an event. A 0 means that the counter can be used.
	 */
	unsigned long used_mask[BITS_TO_LONGS(CSKY_PMU_MAX_EVENTS)];
};

static uint64_t (*hw_raw_read_mapping[CSKY_PMU_MAX_EVENTS])(void);
static void (*hw_raw_write_mapping[CSKY_PMU_MAX_EVENTS])(uint64_t val);

static struct csky_pmu_t {
	struct pmu			pmu;
	struct pmu_hw_events __percpu	*hw_events;
	struct platform_device		*plat_device;
	uint32_t			count_width;
	uint32_t			hpcr;
	u64				max_period;
} csky_pmu;
static int csky_pmu_irq;

#define to_csky_pmu(p)  (container_of(p, struct csky_pmu, pmu))

#define cprgr(reg)				\
({						\
	unsigned int tmp;			\
	asm volatile("cprgr %0, "reg"\n"	\
		     : "=r"(tmp)		\
		     :				\
		     : "memory");		\
	tmp;					\
})

#define cpwgr(reg, val)		\
({				\
	asm volatile(		\
	"cpwgr %0, "reg"\n"	\
	:			\
	: "r"(val)		\
	: "memory");		\
})

#define cprcr(reg)				\
({						\
	unsigned int tmp;			\
	asm volatile("cprcr %0, "reg"\n"	\
		     : "=r"(tmp)		\
		     :				\
		     : "memory");		\
	tmp;					\
})

#define cpwcr(reg, val)		\
({				\
	asm volatile(		\
	"cpwcr %0, "reg"\n"	\
	:			\
	: "r"(val)		\
	: "memory");		\
})

/* cycle counter */
uint64_t csky_pmu_read_cc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x3>");
		lo  = cprgr("<0, 0x2>");
		hi  = cprgr("<0, 0x3>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_cc(uint64_t val)
{
	cpwgr("<0, 0x2>", (uint32_t)  val);
	cpwgr("<0, 0x3>", (uint32_t) (val >> 32));
}

/* instruction counter */
static uint64_t csky_pmu_read_ic(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x5>");
		lo  = cprgr("<0, 0x4>");
		hi  = cprgr("<0, 0x5>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_ic(uint64_t val)
{
	cpwgr("<0, 0x4>", (uint32_t)  val);
	cpwgr("<0, 0x5>", (uint32_t) (val >> 32));
}

/* l1 icache access counter */
static uint64_t csky_pmu_read_icac(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x7>");
		lo  = cprgr("<0, 0x6>");
		hi  = cprgr("<0, 0x7>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_icac(uint64_t val)
{
	cpwgr("<0, 0x6>", (uint32_t)  val);
	cpwgr("<0, 0x7>", (uint32_t) (val >> 32));
}

/* l1 icache miss counter */
static uint64_t csky_pmu_read_icmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x9>");
		lo  = cprgr("<0, 0x8>");
		hi  = cprgr("<0, 0x9>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_icmc(uint64_t val)
{
	cpwgr("<0, 0x8>", (uint32_t)  val);
	cpwgr("<0, 0x9>", (uint32_t) (val >> 32));
}

/* l1 dcache access counter */
static uint64_t csky_pmu_read_dcac(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0xb>");
		lo  = cprgr("<0, 0xa>");
		hi  = cprgr("<0, 0xb>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_dcac(uint64_t val)
{
	cpwgr("<0, 0xa>", (uint32_t)  val);
	cpwgr("<0, 0xb>", (uint32_t) (val >> 32));
}

/* l1 dcache miss counter */
static uint64_t csky_pmu_read_dcmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0xd>");
		lo  = cprgr("<0, 0xc>");
		hi  = cprgr("<0, 0xd>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_dcmc(uint64_t val)
{
	cpwgr("<0, 0xc>", (uint32_t)  val);
	cpwgr("<0, 0xd>", (uint32_t) (val >> 32));
}

/* l2 cache access counter */
static uint64_t csky_pmu_read_l2ac(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0xf>");
		lo  = cprgr("<0, 0xe>");
		hi  = cprgr("<0, 0xf>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_l2ac(uint64_t val)
{
	cpwgr("<0, 0xe>", (uint32_t)  val);
	cpwgr("<0, 0xf>", (uint32_t) (val >> 32));
}

/* l2 cache miss counter */
static uint64_t csky_pmu_read_l2mc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x11>");
		lo  = cprgr("<0, 0x10>");
		hi  = cprgr("<0, 0x11>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_l2mc(uint64_t val)
{
	cpwgr("<0, 0x10>", (uint32_t)  val);
	cpwgr("<0, 0x11>", (uint32_t) (val >> 32));
}

/* I-UTLB miss counter */
static uint64_t csky_pmu_read_iutlbmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x15>");
		lo  = cprgr("<0, 0x14>");
		hi  = cprgr("<0, 0x15>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_iutlbmc(uint64_t val)
{
	cpwgr("<0, 0x14>", (uint32_t)  val);
	cpwgr("<0, 0x15>", (uint32_t) (val >> 32));
}

/* D-UTLB miss counter */
static uint64_t csky_pmu_read_dutlbmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x17>");
		lo  = cprgr("<0, 0x16>");
		hi  = cprgr("<0, 0x17>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_dutlbmc(uint64_t val)
{
	cpwgr("<0, 0x16>", (uint32_t)  val);
	cpwgr("<0, 0x17>", (uint32_t) (val >> 32));
}

/* JTLB miss counter */
static uint64_t csky_pmu_read_jtlbmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x19>");
		lo  = cprgr("<0, 0x18>");
		hi  = cprgr("<0, 0x19>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_jtlbmc(uint64_t val)
{
	cpwgr("<0, 0x18>", (uint32_t)  val);
	cpwgr("<0, 0x19>", (uint32_t) (val >> 32));
}

/* software counter */
static uint64_t csky_pmu_read_softc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x1b>");
		lo  = cprgr("<0, 0x1a>");
		hi  = cprgr("<0, 0x1b>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_softc(uint64_t val)
{
	cpwgr("<0, 0x1a>", (uint32_t)  val);
	cpwgr("<0, 0x1b>", (uint32_t) (val >> 32));
}

/* conditional branch mispredict counter */
static uint64_t csky_pmu_read_cbmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x1d>");
		lo  = cprgr("<0, 0x1c>");
		hi  = cprgr("<0, 0x1d>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_cbmc(uint64_t val)
{
	cpwgr("<0, 0x1c>", (uint32_t)  val);
	cpwgr("<0, 0x1d>", (uint32_t) (val >> 32));
}

/* conditional branch instruction counter */
static uint64_t csky_pmu_read_cbic(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x1f>");
		lo  = cprgr("<0, 0x1e>");
		hi  = cprgr("<0, 0x1f>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_cbic(uint64_t val)
{
	cpwgr("<0, 0x1e>", (uint32_t)  val);
	cpwgr("<0, 0x1f>", (uint32_t) (val >> 32));
}

/* indirect branch mispredict counter */
static uint64_t csky_pmu_read_ibmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x21>");
		lo  = cprgr("<0, 0x20>");
		hi  = cprgr("<0, 0x21>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_ibmc(uint64_t val)
{
	cpwgr("<0, 0x20>", (uint32_t)  val);
	cpwgr("<0, 0x21>", (uint32_t) (val >> 32));
}

/* indirect branch instruction counter */
static uint64_t csky_pmu_read_ibic(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x23>");
		lo  = cprgr("<0, 0x22>");
		hi  = cprgr("<0, 0x23>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_ibic(uint64_t val)
{
	cpwgr("<0, 0x22>", (uint32_t)  val);
	cpwgr("<0, 0x23>", (uint32_t) (val >> 32));
}

/* LSU spec fail counter */
static uint64_t csky_pmu_read_lsfc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x25>");
		lo  = cprgr("<0, 0x24>");
		hi  = cprgr("<0, 0x25>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_lsfc(uint64_t val)
{
	cpwgr("<0, 0x24>", (uint32_t)  val);
	cpwgr("<0, 0x25>", (uint32_t) (val >> 32));
}

/* store instruction counter */
static uint64_t csky_pmu_read_sic(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x27>");
		lo  = cprgr("<0, 0x26>");
		hi  = cprgr("<0, 0x27>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_sic(uint64_t val)
{
	cpwgr("<0, 0x26>", (uint32_t)  val);
	cpwgr("<0, 0x27>", (uint32_t) (val >> 32));
}

/* dcache read access counter */
static uint64_t csky_pmu_read_dcrac(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x29>");
		lo  = cprgr("<0, 0x28>");
		hi  = cprgr("<0, 0x29>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_dcrac(uint64_t val)
{
	cpwgr("<0, 0x28>", (uint32_t)  val);
	cpwgr("<0, 0x29>", (uint32_t) (val >> 32));
}

/* dcache read miss counter */
static uint64_t csky_pmu_read_dcrmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x2b>");
		lo  = cprgr("<0, 0x2a>");
		hi  = cprgr("<0, 0x2b>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_dcrmc(uint64_t val)
{
	cpwgr("<0, 0x2a>", (uint32_t)  val);
	cpwgr("<0, 0x2b>", (uint32_t) (val >> 32));
}

/* dcache write access counter */
static uint64_t csky_pmu_read_dcwac(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x2d>");
		lo  = cprgr("<0, 0x2c>");
		hi  = cprgr("<0, 0x2d>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_dcwac(uint64_t val)
{
	cpwgr("<0, 0x2c>", (uint32_t)  val);
	cpwgr("<0, 0x2d>", (uint32_t) (val >> 32));
}

/* dcache write miss counter */
static uint64_t csky_pmu_read_dcwmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x2f>");
		lo  = cprgr("<0, 0x2e>");
		hi  = cprgr("<0, 0x2f>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_dcwmc(uint64_t val)
{
	cpwgr("<0, 0x2e>", (uint32_t)  val);
	cpwgr("<0, 0x2f>", (uint32_t) (val >> 32));
}

/* l2cache read access counter */
static uint64_t csky_pmu_read_l2rac(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x31>");
		lo  = cprgr("<0, 0x30>");
		hi  = cprgr("<0, 0x31>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_l2rac(uint64_t val)
{
	cpwgr("<0, 0x30>", (uint32_t)  val);
	cpwgr("<0, 0x31>", (uint32_t) (val >> 32));
}

/* l2cache read miss counter */
static uint64_t csky_pmu_read_l2rmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x33>");
		lo  = cprgr("<0, 0x32>");
		hi  = cprgr("<0, 0x33>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_l2rmc(uint64_t val)
{
	cpwgr("<0, 0x32>", (uint32_t)  val);
	cpwgr("<0, 0x33>", (uint32_t) (val >> 32));
}

/* l2cache write access counter */
static uint64_t csky_pmu_read_l2wac(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x35>");
		lo  = cprgr("<0, 0x34>");
		hi  = cprgr("<0, 0x35>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_l2wac(uint64_t val)
{
	cpwgr("<0, 0x34>", (uint32_t)  val);
	cpwgr("<0, 0x35>", (uint32_t) (val >> 32));
}

/* l2cache write miss counter */
static uint64_t csky_pmu_read_l2wmc(void)
{
	uint32_t lo, hi, tmp;
	uint64_t result;

	do {
		tmp = cprgr("<0, 0x37>");
		lo  = cprgr("<0, 0x36>");
		hi  = cprgr("<0, 0x37>");
	} while (hi != tmp);

	result = (uint64_t) (hi) << 32;
	result |= lo;

	return result;
}

static void csky_pmu_write_l2wmc(uint64_t val)
{
	cpwgr("<0, 0x36>", (uint32_t)  val);
	cpwgr("<0, 0x37>", (uint32_t) (val >> 32));
}

#define HW_OP_UNSUPPORTED	0xffff
static const int csky_pmu_hw_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= 0x1,
	[PERF_COUNT_HW_INSTRUCTIONS]		= 0x2,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]		= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0xf,
	[PERF_COUNT_HW_BRANCH_MISSES]		= 0xe,
	[PERF_COUNT_HW_BUS_CYCLES]		= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_REF_CPU_CYCLES]		= HW_OP_UNSUPPORTED,
};

#define C(_x)			PERF_COUNT_HW_CACHE_##_x
#define CACHE_OP_UNSUPPORTED	0xffff
static const int csky_pmu_cache_map[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
	[C(L1D)] = {
#ifdef CONFIG_CPU_CK810
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= 0x5,
			[C(RESULT_MISS)]	= 0x6,
		},
#else
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= 0x14,
			[C(RESULT_MISS)]	= 0x15,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= 0x16,
			[C(RESULT_MISS)]	= 0x17,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
#endif
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= 0x3,
			[C(RESULT_MISS)]	= 0x4,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
#ifdef CONFIG_CPU_CK810
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= 0x7,
			[C(RESULT_MISS)]	= 0x8,
		},
#else
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= 0x18,
			[C(RESULT_MISS)]	= 0x19,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= 0x1a,
			[C(RESULT_MISS)]	= 0x1b,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
#endif
	},
	[C(DTLB)] = {
#ifdef CONFIG_CPU_CK810
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
#else
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= 0x14,
			[C(RESULT_MISS)]	= 0xb,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= 0x16,
			[C(RESULT_MISS)]	= 0xb,
		},
#endif
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
#ifdef CONFIG_CPU_CK810
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
#else
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= 0x3,
			[C(RESULT_MISS)]	= 0xa,
		},
#endif
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(NODE)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
};

int  csky_pmu_event_set_period(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	s64 left = local64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int ret = 0;

	if (unlikely(left <= -period)) {
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (left > (s64)csky_pmu.max_period)
		left = csky_pmu.max_period;

	/*
	 * The hw event starts counting from this event offset,
	 * mark it to be able to extract future "deltas":
	 */
	local64_set(&hwc->prev_count, (u64)(-left));

	if (hw_raw_write_mapping[hwc->idx] != NULL)
		hw_raw_write_mapping[hwc->idx]((u64)(-left) &
						csky_pmu.max_period);

	cpwcr(HPOFSR, ~BIT(hwc->idx) & cprcr(HPOFSR));

	perf_event_update_userpage(event);

	return ret;
}

static void csky_perf_event_update(struct perf_event *event,
				   struct hw_perf_event *hwc)
{
	uint64_t prev_raw_count = local64_read(&hwc->prev_count);
	/*
	 * Sign extend count value to 64bit, otherwise delta calculation
	 * would be incorrect when overflow occurs.
	 */
	uint64_t new_raw_count = sign_extend64(
		hw_raw_read_mapping[hwc->idx](), csky_pmu.count_width - 1);
	int64_t delta = new_raw_count - prev_raw_count;

	/*
	 * We aren't afraid of hwc->prev_count changing beneath our feet
	 * because there's no way for us to re-enter this function anytime.
	 */
	local64_set(&hwc->prev_count, new_raw_count);
	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);
}

static void csky_pmu_reset(void *info)
{
	cpwcr(HPCR, BIT(31) | BIT(30) | BIT(1));
}

static void csky_pmu_read(struct perf_event *event)
{
	csky_perf_event_update(event, &event->hw);
}

static int csky_pmu_cache_event(u64 config)
{
	unsigned int cache_type, cache_op, cache_result;

	cache_type	= (config >>  0) & 0xff;
	cache_op	= (config >>  8) & 0xff;
	cache_result	= (config >> 16) & 0xff;

	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return -EINVAL;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return -EINVAL;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	return csky_pmu_cache_map[cache_type][cache_op][cache_result];
}

static int csky_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int ret;

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		if (event->attr.config >= PERF_COUNT_HW_MAX)
			return -ENOENT;
		ret = csky_pmu_hw_map[event->attr.config];
		if (ret == HW_OP_UNSUPPORTED)
			return -ENOENT;
		hwc->idx = ret;
		break;
	case PERF_TYPE_HW_CACHE:
		ret = csky_pmu_cache_event(event->attr.config);
		if (ret == CACHE_OP_UNSUPPORTED)
			return -ENOENT;
		hwc->idx = ret;
		break;
	case PERF_TYPE_RAW:
		if (hw_raw_read_mapping[event->attr.config] == NULL)
			return -ENOENT;
		hwc->idx = event->attr.config;
		break;
	default:
		return -ENOENT;
	}

	if (event->attr.exclude_user)
		csky_pmu.hpcr = BIT(2);
	else if (event->attr.exclude_kernel)
		csky_pmu.hpcr = BIT(3);
	else
		csky_pmu.hpcr = BIT(2) | BIT(3);

	csky_pmu.hpcr |= BIT(1) | BIT(0);

	return 0;
}

/* starts all counters */
static void csky_pmu_enable(struct pmu *pmu)
{
	cpwcr(HPCR, csky_pmu.hpcr);
}

/* stops all counters */
static void csky_pmu_disable(struct pmu *pmu)
{
	cpwcr(HPCR, BIT(1));
}

static void csky_pmu_start(struct perf_event *event, int flags)
{
	unsigned long flg;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (WARN_ON_ONCE(idx == -1))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));

	hwc->state = 0;

	csky_pmu_event_set_period(event);

	local_irq_save(flg);

	cpwcr(HPINTENR, BIT(idx) | cprcr(HPINTENR));
	cpwcr(HPCNTENR, BIT(idx) | cprcr(HPCNTENR));

	local_irq_restore(flg);
}

static void csky_pmu_stop_event(struct perf_event *event)
{
	unsigned long flg;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	local_irq_save(flg);

	cpwcr(HPINTENR, ~BIT(idx) & cprcr(HPINTENR));
	cpwcr(HPCNTENR, ~BIT(idx) & cprcr(HPCNTENR));

	local_irq_restore(flg);
}

static void csky_pmu_stop(struct perf_event *event, int flags)
{
	if (!(event->hw.state & PERF_HES_STOPPED)) {
		csky_pmu_stop_event(event);
		event->hw.state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) &&
	    !(event->hw.state & PERF_HES_UPTODATE)) {
		csky_perf_event_update(event, &event->hw);
		event->hw.state |= PERF_HES_UPTODATE;
	}
}

static void csky_pmu_del(struct perf_event *event, int flags)
{
	struct pmu_hw_events *hw_events = this_cpu_ptr(csky_pmu.hw_events);
	struct hw_perf_event *hwc = &event->hw;

	csky_pmu_stop(event, PERF_EF_UPDATE);

	hw_events->events[hwc->idx] = NULL;

	perf_event_update_userpage(event);
}

/* allocate hardware counter and optionally start counting */
static int csky_pmu_add(struct perf_event *event, int flags)
{
	struct pmu_hw_events *hw_events = this_cpu_ptr(csky_pmu.hw_events);
	struct hw_perf_event *hwc = &event->hw;

	hw_events->events[hwc->idx] = event;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		csky_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);

	return 0;
}

static irqreturn_t csky_pmu_handle_irq(int irq_num, void *dev)
{
	struct perf_sample_data data;
	struct pmu_hw_events *cpuc = this_cpu_ptr(csky_pmu.hw_events);
	struct pt_regs *regs;
	int idx;

	/*
	 * Did an overflow occur?
	 */
	if (!cprcr(HPOFSR))
		return IRQ_NONE;

	/*
	 * Handle the counter(s) overflow(s)
	 */
	regs = get_irq_regs();

	csky_pmu_disable(&csky_pmu.pmu);

	for (idx = 0; idx < CSKY_PMU_MAX_EVENTS; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		/* Ignore if we don't have an event. */
		if (!event)
			continue;
		/*
		 * We have a single interrupt for all counters. Check that
		 * each counter has overflowed before we process it.
		 */
		if (!(cprcr(HPOFSR) & BIT(idx)))
			continue;

		hwc = &event->hw;
		csky_perf_event_update(event, &event->hw);
		perf_sample_data_init(&data, 0, hwc->last_period);
		csky_pmu_event_set_period(event);

		perf_event_overflow(event, &data, regs);
	}

	csky_pmu_enable(&csky_pmu.pmu);

	/*
	 * Handle the pending perf events.
	 *
	 * Note: this call *must* be run with interrupts disabled. For
	 * platforms that can have the PMU interrupts raised as an NMI, this
	 * will not work.
	 */
	irq_work_run();

	return IRQ_HANDLED;
}

static int csky_pmu_request_irq(irq_handler_t handler)
{
	int err, irqs;
	struct platform_device *pmu_device = csky_pmu.plat_device;

	if (!pmu_device)
		return -ENODEV;

	irqs = min(pmu_device->num_resources, num_possible_cpus());
	if (irqs < 1) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	csky_pmu_irq = platform_get_irq(pmu_device, 0);
	if (csky_pmu_irq < 0)
		return -ENODEV;
	err = request_percpu_irq(csky_pmu_irq, handler, "csky-pmu",
				 this_cpu_ptr(csky_pmu.hw_events));
	if (err) {
		pr_err("unable to request IRQ%d for CSKY PMU counters\n",
		       csky_pmu_irq);
		return err;
	}

	return 0;
}

static void csky_pmu_free_irq(void)
{
	int irq;
	struct platform_device *pmu_device = csky_pmu.plat_device;

	irq = platform_get_irq(pmu_device, 0);
	if (irq >= 0)
		free_percpu_irq(irq, this_cpu_ptr(csky_pmu.hw_events));
}

int init_hw_perf_events(void)
{
	csky_pmu.hw_events = alloc_percpu_gfp(struct pmu_hw_events,
					      GFP_KERNEL);
	if (!csky_pmu.hw_events) {
		pr_info("failed to allocate per-cpu PMU data.\n");
		return -ENOMEM;
	}

	csky_pmu.pmu = (struct pmu) {
		.pmu_enable	= csky_pmu_enable,
		.pmu_disable	= csky_pmu_disable,
		.event_init	= csky_pmu_event_init,
		.add		= csky_pmu_add,
		.del		= csky_pmu_del,
		.start		= csky_pmu_start,
		.stop		= csky_pmu_stop,
		.read		= csky_pmu_read,
	};

	memset((void *)hw_raw_read_mapping, 0,
		sizeof(hw_raw_read_mapping[CSKY_PMU_MAX_EVENTS]));

	hw_raw_read_mapping[0x1]  = csky_pmu_read_cc;
	hw_raw_read_mapping[0x2]  = csky_pmu_read_ic;
	hw_raw_read_mapping[0x3]  = csky_pmu_read_icac;
	hw_raw_read_mapping[0x4]  = csky_pmu_read_icmc;
	hw_raw_read_mapping[0x5]  = csky_pmu_read_dcac;
	hw_raw_read_mapping[0x6]  = csky_pmu_read_dcmc;
	hw_raw_read_mapping[0x7]  = csky_pmu_read_l2ac;
	hw_raw_read_mapping[0x8]  = csky_pmu_read_l2mc;
	hw_raw_read_mapping[0xa]  = csky_pmu_read_iutlbmc;
	hw_raw_read_mapping[0xb]  = csky_pmu_read_dutlbmc;
	hw_raw_read_mapping[0xc]  = csky_pmu_read_jtlbmc;
	hw_raw_read_mapping[0xd]  = csky_pmu_read_softc;
	hw_raw_read_mapping[0xe]  = csky_pmu_read_cbmc;
	hw_raw_read_mapping[0xf]  = csky_pmu_read_cbic;
	hw_raw_read_mapping[0x10] = csky_pmu_read_ibmc;
	hw_raw_read_mapping[0x11] = csky_pmu_read_ibic;
	hw_raw_read_mapping[0x12] = csky_pmu_read_lsfc;
	hw_raw_read_mapping[0x13] = csky_pmu_read_sic;
	hw_raw_read_mapping[0x14] = csky_pmu_read_dcrac;
	hw_raw_read_mapping[0x15] = csky_pmu_read_dcrmc;
	hw_raw_read_mapping[0x16] = csky_pmu_read_dcwac;
	hw_raw_read_mapping[0x17] = csky_pmu_read_dcwmc;
	hw_raw_read_mapping[0x18] = csky_pmu_read_l2rac;
	hw_raw_read_mapping[0x19] = csky_pmu_read_l2rmc;
	hw_raw_read_mapping[0x1a] = csky_pmu_read_l2wac;
	hw_raw_read_mapping[0x1b] = csky_pmu_read_l2wmc;

	memset((void *)hw_raw_write_mapping, 0,
		sizeof(hw_raw_write_mapping[CSKY_PMU_MAX_EVENTS]));

	hw_raw_write_mapping[0x1]  = csky_pmu_write_cc;
	hw_raw_write_mapping[0x2]  = csky_pmu_write_ic;
	hw_raw_write_mapping[0x3]  = csky_pmu_write_icac;
	hw_raw_write_mapping[0x4]  = csky_pmu_write_icmc;
	hw_raw_write_mapping[0x5]  = csky_pmu_write_dcac;
	hw_raw_write_mapping[0x6]  = csky_pmu_write_dcmc;
	hw_raw_write_mapping[0x7]  = csky_pmu_write_l2ac;
	hw_raw_write_mapping[0x8]  = csky_pmu_write_l2mc;
	hw_raw_write_mapping[0xa]  = csky_pmu_write_iutlbmc;
	hw_raw_write_mapping[0xb]  = csky_pmu_write_dutlbmc;
	hw_raw_write_mapping[0xc]  = csky_pmu_write_jtlbmc;
	hw_raw_write_mapping[0xd]  = csky_pmu_write_softc;
	hw_raw_write_mapping[0xe]  = csky_pmu_write_cbmc;
	hw_raw_write_mapping[0xf]  = csky_pmu_write_cbic;
	hw_raw_write_mapping[0x10] = csky_pmu_write_ibmc;
	hw_raw_write_mapping[0x11] = csky_pmu_write_ibic;
	hw_raw_write_mapping[0x12] = csky_pmu_write_lsfc;
	hw_raw_write_mapping[0x13] = csky_pmu_write_sic;
	hw_raw_write_mapping[0x14] = csky_pmu_write_dcrac;
	hw_raw_write_mapping[0x15] = csky_pmu_write_dcrmc;
	hw_raw_write_mapping[0x16] = csky_pmu_write_dcwac;
	hw_raw_write_mapping[0x17] = csky_pmu_write_dcwmc;
	hw_raw_write_mapping[0x18] = csky_pmu_write_l2rac;
	hw_raw_write_mapping[0x19] = csky_pmu_write_l2rmc;
	hw_raw_write_mapping[0x1a] = csky_pmu_write_l2wac;
	hw_raw_write_mapping[0x1b] = csky_pmu_write_l2wmc;

	return 0;
}

static int csky_pmu_starting_cpu(unsigned int cpu)
{
	enable_percpu_irq(csky_pmu_irq, 0);
	return 0;
}

static int csky_pmu_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(csky_pmu_irq);
	return 0;
}

int csky_pmu_device_probe(struct platform_device *pdev,
			  const struct of_device_id *of_table)
{
	struct device_node *node = pdev->dev.of_node;
	int ret;

	ret = init_hw_perf_events();
	if (ret) {
		pr_notice("[perf] failed to probe PMU!\n");
		return ret;
	}

	if (of_property_read_u32(node, "count-width",
				 &csky_pmu.count_width)) {
		csky_pmu.count_width = DEFAULT_COUNT_WIDTH;
	}
	csky_pmu.max_period = BIT_ULL(csky_pmu.count_width) - 1;

	csky_pmu.plat_device = pdev;

	/* Ensure the PMU has sane values out of reset. */
	on_each_cpu(csky_pmu_reset, &csky_pmu, 1);

	ret = csky_pmu_request_irq(csky_pmu_handle_irq);
	if (ret) {
		csky_pmu.pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;
		pr_notice("[perf] PMU request irq fail!\n");
	}

	ret = cpuhp_setup_state(CPUHP_AP_PERF_CSKY_ONLINE, "AP_PERF_ONLINE",
				csky_pmu_starting_cpu,
				csky_pmu_dying_cpu);
	if (ret) {
		csky_pmu_free_irq();
		free_percpu(csky_pmu.hw_events);
		return ret;
	}

	ret = perf_pmu_register(&csky_pmu.pmu, "cpu", PERF_TYPE_RAW);
	if (ret) {
		csky_pmu_free_irq();
		free_percpu(csky_pmu.hw_events);
	}

	return ret;
}

static const struct of_device_id csky_pmu_of_device_ids[] = {
	{.compatible = "csky,csky-pmu"},
	{},
};

static int csky_pmu_dev_probe(struct platform_device *pdev)
{
	return csky_pmu_device_probe(pdev, csky_pmu_of_device_ids);
}

static struct platform_driver csky_pmu_driver = {
	.driver = {
		   .name = "csky-pmu",
		   .of_match_table = csky_pmu_of_device_ids,
		   },
	.probe = csky_pmu_dev_probe,
};

static int __init csky_pmu_probe(void)
{
	int ret;

	ret = platform_driver_register(&csky_pmu_driver);
	if (ret)
		pr_notice("[perf] PMU initialization failed\n");
	else
		pr_notice("[perf] PMU initialization done\n");

	return ret;
}

device_initcall(csky_pmu_probe);
