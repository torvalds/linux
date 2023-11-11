
/*
 * struct hw_perf_event.flags flags
 */
PERF_ARCH(PEBS_LDLAT,		0x00001) /* ld+ldlat data address sampling */
PERF_ARCH(PEBS_ST,		0x00002) /* st data address sampling */
PERF_ARCH(PEBS_ST_HSW,		0x00004) /* haswell style datala, store */
PERF_ARCH(PEBS_LD_HSW,		0x00008) /* haswell style datala, load */
PERF_ARCH(PEBS_NA_HSW,		0x00010) /* haswell style datala, unknown */
PERF_ARCH(EXCL,			0x00020) /* HT exclusivity on counter */
PERF_ARCH(DYNAMIC,		0x00040) /* dynamic alloc'd constraint */
			/*	0x00080	*/
PERF_ARCH(EXCL_ACCT,		0x00100) /* accounted EXCL event */
PERF_ARCH(AUTO_RELOAD,		0x00200) /* use PEBS auto-reload */
PERF_ARCH(LARGE_PEBS,		0x00400) /* use large PEBS */
PERF_ARCH(PEBS_VIA_PT,		0x00800) /* use PT buffer for PEBS */
PERF_ARCH(PAIR,			0x01000) /* Large Increment per Cycle */
PERF_ARCH(LBR_SELECT,		0x02000) /* Save/Restore MSR_LBR_SELECT */
PERF_ARCH(TOPDOWN,		0x04000) /* Count Topdown slots/metrics events */
PERF_ARCH(PEBS_STLAT,		0x08000) /* st+stlat data address sampling */
PERF_ARCH(AMD_BRS,		0x10000) /* AMD Branch Sampling */
PERF_ARCH(PEBS_LAT_HYBRID,	0x20000) /* ld and st lat for hybrid */
