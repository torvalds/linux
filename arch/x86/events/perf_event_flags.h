
/*
 * struct hw_perf_event.flags flags
 */
PERF_ARCH(PEBS_LDLAT,		0x0000001) /* ld+ldlat data address sampling */
PERF_ARCH(PEBS_ST,		0x0000002) /* st data address sampling */
PERF_ARCH(PEBS_ST_HSW,		0x0000004) /* haswell style datala, store */
PERF_ARCH(PEBS_LD_HSW,		0x0000008) /* haswell style datala, load */
PERF_ARCH(PEBS_NA_HSW,		0x0000010) /* haswell style datala, unknown */
PERF_ARCH(EXCL,			0x0000020) /* HT exclusivity on counter */
PERF_ARCH(DYNAMIC,		0x0000040) /* dynamic alloc'd constraint */
PERF_ARCH(PEBS_CNTR,		0x0000080) /* PEBS counters snapshot */
PERF_ARCH(EXCL_ACCT,		0x0000100) /* accounted EXCL event */
PERF_ARCH(AUTO_RELOAD,		0x0000200) /* use PEBS auto-reload */
PERF_ARCH(LARGE_PEBS,		0x0000400) /* use large PEBS */
PERF_ARCH(PEBS_VIA_PT,		0x0000800) /* use PT buffer for PEBS */
PERF_ARCH(PAIR,			0x0001000) /* Large Increment per Cycle */
PERF_ARCH(LBR_SELECT,		0x0002000) /* Save/Restore MSR_LBR_SELECT */
PERF_ARCH(TOPDOWN,		0x0004000) /* Count Topdown slots/metrics events */
PERF_ARCH(PEBS_STLAT,		0x0008000) /* st+stlat data address sampling */
PERF_ARCH(AMD_BRS,		0x0010000) /* AMD Branch Sampling */
PERF_ARCH(PEBS_LAT_HYBRID,	0x0020000) /* ld and st lat for hybrid */
PERF_ARCH(NEEDS_BRANCH_STACK,	0x0040000) /* require branch stack setup */
PERF_ARCH(BRANCH_COUNTERS,	0x0080000) /* logs the counters in the extra space of each branch */
PERF_ARCH(ACR,			0x0100000) /* Auto counter reload */
