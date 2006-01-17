/*
 * This file contains the Montecito PMU register description tables
 * and pmc checker used by perfmon.c.
 *
 * Copyright (c) 2005-2006 Hewlett-Packard Development Company, L.P.
 *               Contributed by Stephane Eranian <eranian@hpl.hp.com>
 */
static int pfm_mont_pmc_check(struct task_struct *task, pfm_context_t *ctx, unsigned int cnum, unsigned long *val, struct pt_regs *regs);

#define RDEP_MONT_ETB	(RDEP(38)|RDEP(39)|RDEP(48)|RDEP(49)|RDEP(50)|RDEP(51)|RDEP(52)|RDEP(53)|RDEP(54)|\
			 RDEP(55)|RDEP(56)|RDEP(57)|RDEP(58)|RDEP(59)|RDEP(60)|RDEP(61)|RDEP(62)|RDEP(63))
#define RDEP_MONT_DEAR  (RDEP(32)|RDEP(33)|RDEP(36))
#define RDEP_MONT_IEAR  (RDEP(34)|RDEP(35))

static pfm_reg_desc_t pfm_mont_pmc_desc[PMU_MAX_PMCS]={
/* pmc0  */ { PFM_REG_CONTROL , 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc1  */ { PFM_REG_CONTROL , 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc2  */ { PFM_REG_CONTROL , 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc3  */ { PFM_REG_CONTROL , 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc4  */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(4),0, 0, 0}, {0,0, 0, 0}},
/* pmc5  */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(5),0, 0, 0}, {0,0, 0, 0}},
/* pmc6  */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(6),0, 0, 0}, {0,0, 0, 0}},
/* pmc7  */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(7),0, 0, 0}, {0,0, 0, 0}},
/* pmc8  */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(8),0, 0, 0}, {0,0, 0, 0}},
/* pmc9  */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(9),0, 0, 0}, {0,0, 0, 0}},
/* pmc10 */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(10),0, 0, 0}, {0,0, 0, 0}},
/* pmc11 */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(11),0, 0, 0}, {0,0, 0, 0}},
/* pmc12 */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(12),0, 0, 0}, {0,0, 0, 0}},
/* pmc13 */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(13),0, 0, 0}, {0,0, 0, 0}},
/* pmc14 */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(14),0, 0, 0}, {0,0, 0, 0}},
/* pmc15 */ { PFM_REG_COUNTING, 6, 0x2000000, 0x7c7fff7f, NULL, pfm_mont_pmc_check, {RDEP(15),0, 0, 0}, {0,0, 0, 0}},
/* pmc16 */ { PFM_REG_NOTIMPL, },
/* pmc17 */ { PFM_REG_NOTIMPL, },
/* pmc18 */ { PFM_REG_NOTIMPL, },
/* pmc19 */ { PFM_REG_NOTIMPL, },
/* pmc20 */ { PFM_REG_NOTIMPL, },
/* pmc21 */ { PFM_REG_NOTIMPL, },
/* pmc22 */ { PFM_REG_NOTIMPL, },
/* pmc23 */ { PFM_REG_NOTIMPL, },
/* pmc24 */ { PFM_REG_NOTIMPL, },
/* pmc25 */ { PFM_REG_NOTIMPL, },
/* pmc26 */ { PFM_REG_NOTIMPL, },
/* pmc27 */ { PFM_REG_NOTIMPL, },
/* pmc28 */ { PFM_REG_NOTIMPL, },
/* pmc29 */ { PFM_REG_NOTIMPL, },
/* pmc30 */ { PFM_REG_NOTIMPL, },
/* pmc31 */ { PFM_REG_NOTIMPL, },
/* pmc32 */ { PFM_REG_CONFIG,  0, 0x30f01ffffffffff, 0x30f01ffffffffff, NULL, pfm_mont_pmc_check, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc33 */ { PFM_REG_CONFIG,  0, 0x0,  0x1ffffffffff, NULL, pfm_mont_pmc_check, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc34 */ { PFM_REG_CONFIG,  0, 0xf01ffffffffff, 0xf01ffffffffff, NULL, pfm_mont_pmc_check, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc35 */ { PFM_REG_CONFIG,  0, 0x0,  0x1ffffffffff, NULL, pfm_mont_pmc_check, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc36 */ { PFM_REG_CONFIG,  0, 0xfffffff0, 0xf, NULL, pfm_mont_pmc_check, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc37 */ { PFM_REG_MONITOR, 4, 0x0, 0x3fff, NULL, pfm_mont_pmc_check, {RDEP_MONT_IEAR, 0, 0, 0}, {0, 0, 0, 0}},
/* pmc38 */ { PFM_REG_CONFIG,  0, 0xdb6, 0x2492, NULL, pfm_mont_pmc_check, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc39 */ { PFM_REG_MONITOR, 6, 0x0, 0xffcf, NULL, pfm_mont_pmc_check, {RDEP_MONT_ETB,0, 0, 0}, {0,0, 0, 0}},
/* pmc40 */ { PFM_REG_MONITOR, 6, 0x2000000, 0xf01cf, NULL, pfm_mont_pmc_check, {RDEP_MONT_DEAR,0, 0, 0}, {0,0, 0, 0}},
/* pmc41 */ { PFM_REG_CONFIG,  0, 0x00002078fefefefe, 0x1e00018181818, NULL, pfm_mont_pmc_check, {0,0, 0, 0}, {0,0, 0, 0}},
/* pmc42 */ { PFM_REG_MONITOR, 6, 0x0, 0x7ff4f, NULL, pfm_mont_pmc_check, {RDEP_MONT_ETB,0, 0, 0}, {0,0, 0, 0}},
	    { PFM_REG_END    , 0, 0x0, -1, NULL, NULL, {0,}, {0,}}, /* end marker */
};

static pfm_reg_desc_t pfm_mont_pmd_desc[PMU_MAX_PMDS]={
/* pmd0  */ { PFM_REG_NOTIMPL, }, 
/* pmd1  */ { PFM_REG_NOTIMPL, },
/* pmd2  */ { PFM_REG_NOTIMPL, },
/* pmd3  */ { PFM_REG_NOTIMPL, },
/* pmd4  */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(4),0, 0, 0}},
/* pmd5  */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(5),0, 0, 0}},
/* pmd6  */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(6),0, 0, 0}},
/* pmd7  */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(7),0, 0, 0}},
/* pmd8  */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(8),0, 0, 0}}, 
/* pmd9  */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(9),0, 0, 0}},
/* pmd10 */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(10),0, 0, 0}},
/* pmd11 */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(11),0, 0, 0}},
/* pmd12 */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(12),0, 0, 0}},
/* pmd13 */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(13),0, 0, 0}},
/* pmd14 */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(14),0, 0, 0}},
/* pmd15 */ { PFM_REG_COUNTING, 0, 0x0, -1, NULL, NULL, {0,0, 0, 0}, {RDEP(15),0, 0, 0}},
/* pmd16 */ { PFM_REG_NOTIMPL, },
/* pmd17 */ { PFM_REG_NOTIMPL, },
/* pmd18 */ { PFM_REG_NOTIMPL, },
/* pmd19 */ { PFM_REG_NOTIMPL, },
/* pmd20 */ { PFM_REG_NOTIMPL, },
/* pmd21 */ { PFM_REG_NOTIMPL, },
/* pmd22 */ { PFM_REG_NOTIMPL, },
/* pmd23 */ { PFM_REG_NOTIMPL, },
/* pmd24 */ { PFM_REG_NOTIMPL, },
/* pmd25 */ { PFM_REG_NOTIMPL, },
/* pmd26 */ { PFM_REG_NOTIMPL, },
/* pmd27 */ { PFM_REG_NOTIMPL, },
/* pmd28 */ { PFM_REG_NOTIMPL, },
/* pmd29 */ { PFM_REG_NOTIMPL, },
/* pmd30 */ { PFM_REG_NOTIMPL, },
/* pmd31 */ { PFM_REG_NOTIMPL, },
/* pmd32 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP(33)|RDEP(36),0, 0, 0}, {RDEP(40),0, 0, 0}},
/* pmd33 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP(32)|RDEP(36),0, 0, 0}, {RDEP(40),0, 0, 0}},
/* pmd34 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP(35),0, 0, 0}, {RDEP(37),0, 0, 0}},
/* pmd35 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP(34),0, 0, 0}, {RDEP(37),0, 0, 0}},
/* pmd36 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP(32)|RDEP(33),0, 0, 0}, {RDEP(40),0, 0, 0}},
/* pmd37 */ { PFM_REG_NOTIMPL, },
/* pmd38 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd39 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd40 */ { PFM_REG_NOTIMPL, },
/* pmd41 */ { PFM_REG_NOTIMPL, },
/* pmd42 */ { PFM_REG_NOTIMPL, },
/* pmd43 */ { PFM_REG_NOTIMPL, },
/* pmd44 */ { PFM_REG_NOTIMPL, },
/* pmd45 */ { PFM_REG_NOTIMPL, },
/* pmd46 */ { PFM_REG_NOTIMPL, },
/* pmd47 */ { PFM_REG_NOTIMPL, },
/* pmd48 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd49 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd50 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd51 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd52 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd53 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd54 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd55 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd56 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd57 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd58 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd59 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd60 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd61 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd62 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
/* pmd63 */ { PFM_REG_BUFFER, 0, 0x0, -1, NULL, NULL, {RDEP_MONT_ETB,0, 0, 0}, {RDEP(39),0, 0, 0}},
	    { PFM_REG_END   , 0, 0x0, -1, NULL, NULL, {0,}, {0,}}, /* end marker */
};

/*
 * PMC reserved fields must have their power-up values preserved
 */
static int
pfm_mont_reserved(unsigned int cnum, unsigned long *val, struct pt_regs *regs)
{
	unsigned long tmp1, tmp2, ival = *val;

	/* remove reserved areas from user value */
	tmp1 = ival & PMC_RSVD_MASK(cnum);

	/* get reserved fields values */
	tmp2 = PMC_DFL_VAL(cnum) & ~PMC_RSVD_MASK(cnum);

	*val = tmp1 | tmp2;

	DPRINT(("pmc[%d]=0x%lx, mask=0x%lx, reset=0x%lx, val=0x%lx\n",
		  cnum, ival, PMC_RSVD_MASK(cnum), PMC_DFL_VAL(cnum), *val));
	return 0;
}

/*
 * task can be NULL if the context is unloaded
 */
static int
pfm_mont_pmc_check(struct task_struct *task, pfm_context_t *ctx, unsigned int cnum, unsigned long *val, struct pt_regs *regs)
{
	int ret = 0;
	unsigned long val32 = 0, val38 = 0, val41 = 0;
	unsigned long tmpval;
	int check_case1 = 0;
	int is_loaded;

	/* first preserve the reserved fields */
	pfm_mont_reserved(cnum, val, regs);

	tmpval = *val;

	/* sanity check */
	if (ctx == NULL) return -EINVAL;

	is_loaded = ctx->ctx_state == PFM_CTX_LOADED || ctx->ctx_state == PFM_CTX_MASKED;

	/*
	 * we must clear the debug registers if pmc41 has a value which enable
	 * memory pipeline event constraints. In this case we need to clear the
	 * the debug registers if they have not yet been accessed. This is required
	 * to avoid picking stale state.
	 * PMC41 is "active" if:
	 * 	one of the pmc41.cfg_dtagXX field is different from 0x3
	 * AND
	 * 	at the corresponding pmc41.en_dbrpXX is set.
	 * AND
	 *	ctx_fl_using_dbreg == 0  (i.e., dbr not yet used)
	 */
	DPRINT(("cnum=%u val=0x%lx, using_dbreg=%d loaded=%d\n", cnum, tmpval, ctx->ctx_fl_using_dbreg, is_loaded));

	if (cnum == 41 && is_loaded 
	    && (tmpval & 0x1e00000000000) && (tmpval & 0x18181818UL) != 0x18181818UL && ctx->ctx_fl_using_dbreg == 0) {

		DPRINT(("pmc[%d]=0x%lx has active pmc41 settings, clearing dbr\n", cnum, tmpval));

		/* don't mix debug with perfmon */
		if (task && (task->thread.flags & IA64_THREAD_DBG_VALID) != 0) return -EINVAL;

		/*
		 * a count of 0 will mark the debug registers if:
		 * AND
		 */
		ret = pfm_write_ibr_dbr(PFM_DATA_RR, ctx, NULL, 0, regs);
		if (ret) return ret;
	}
	/*
	 * we must clear the (instruction) debug registers if:
	 * 	pmc38.ig_ibrpX is 0 (enabled)
	 * AND
	 *	ctx_fl_using_dbreg == 0  (i.e., dbr not yet used)
	 */
	if (cnum == 38 && is_loaded && ((tmpval & 0x492UL) != 0x492UL) && ctx->ctx_fl_using_dbreg == 0) {

		DPRINT(("pmc38=0x%lx has active pmc38 settings, clearing ibr\n", tmpval));

		/* don't mix debug with perfmon */
		if (task && (task->thread.flags & IA64_THREAD_DBG_VALID) != 0) return -EINVAL;

		/*
		 * a count of 0 will mark the debug registers as in use and also
		 * ensure that they are properly cleared.
		 */
		ret = pfm_write_ibr_dbr(PFM_CODE_RR, ctx, NULL, 0, regs);
		if (ret) return ret;

	}
	switch(cnum) {
		case  32: val32 = *val;
			  val38 = ctx->ctx_pmcs[38];
			  val41 = ctx->ctx_pmcs[41];
			  check_case1 = 1;
			  break;
		case  38: val38 = *val;
			  val32 = ctx->ctx_pmcs[32];
			  val41 = ctx->ctx_pmcs[41];
			  check_case1 = 1;
			  break;
		case  41: val41 = *val;
			  val32 = ctx->ctx_pmcs[32];
			  val38 = ctx->ctx_pmcs[38];
			  check_case1 = 1;
			  break;
	}
	/* check illegal configuration which can produce inconsistencies in tagging
	 * i-side events in L1D and L2 caches
	 */
	if (check_case1) {
		ret =   (((val41 >> 45) & 0xf) == 0 && ((val32>>57) & 0x1) == 0)
		     && ((((val38>>1) & 0x3) == 0x2 || ((val38>>1) & 0x3) == 0)
		     ||  (((val38>>4) & 0x3) == 0x2 || ((val38>>4) & 0x3) == 0));
		if (ret) {
			DPRINT(("invalid config pmc38=0x%lx pmc41=0x%lx pmc32=0x%lx\n", val38, val41, val32));
			return -EINVAL;
		}
	}
	*val = tmpval;
	return 0;
}

/*
 * impl_pmcs, impl_pmds are computed at runtime to minimize errors!
 */
static pmu_config_t pmu_conf_mont={
	.pmu_name        = "Montecito",
	.pmu_family      = 0x20,
	.flags           = PFM_PMU_IRQ_RESEND,
	.ovfl_val        = (1UL << 47) - 1,
	.pmd_desc        = pfm_mont_pmd_desc,
	.pmc_desc        = pfm_mont_pmc_desc,
	.num_ibrs        = 8,
	.num_dbrs        = 8,
	.use_rr_dbregs   = 1 /* debug register are use for range retrictions */
};
