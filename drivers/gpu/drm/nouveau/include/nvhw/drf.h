/*
 * Copyright 2019 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __NVHW_DRF_H__
#define __NVHW_DRF_H__

/* Helpers common to all DRF accessors. */
#define DRF_LO(drf)    (0 ? drf)
#define DRF_HI(drf)    (1 ? drf)
#define DRF_BITS(drf)  (DRF_HI(drf) - DRF_LO(drf) + 1)
#define DRF_MASK(drf)  (~0ULL >> (64 - DRF_BITS(drf)))
#define DRF_SMASK(drf) (DRF_MASK(drf) << DRF_LO(drf))

/* Helpers for DRF-MW accessors. */
#define DRF_MX_MW(drf)      drf
#define DRF_MX(drf)         DRF_MX_##drf
#define DRF_MW(drf)         DRF_MX(drf)
#define DRF_MW_SPANS(o,drf) (DRF_LW_IDX((o),drf) != DRF_HW_IDX((o),drf))
#define DRF_MW_SIZE(o)      (sizeof((o)[0]) * 8)

#define DRF_LW_IDX(o,drf)   (DRF_LO(DRF_MW(drf)) / DRF_MW_SIZE(o))
#define DRF_LW_LO(o,drf)    (DRF_LO(DRF_MW(drf)) % DRF_MW_SIZE(o))
#define DRF_LW_HI(o,drf)    (DRF_MW_SPANS((o),drf) ? (DRF_MW_SIZE(o) - 1) : DRF_HW_HI((o),drf))
#define DRF_LW_BITS(o,drf)  (DRF_LW_HI((o),drf) - DRF_LW_LO((o),drf) + 1)
#define DRF_LW_MASK(o,drf)  (~0ULL >> (64 - DRF_LW_BITS((o),drf)))
#define DRF_LW_SMASK(o,drf) (DRF_LW_MASK((o),drf) << DRF_LW_LO((o),drf))
#define DRF_LW_GET(o,drf)   (((o)[DRF_LW_IDX((o),drf)] >> DRF_LW_LO((o),drf)) & DRF_LW_MASK((o),drf))
#define DRF_LW_VAL(o,drf,v) (((v) & DRF_LW_MASK((o),drf)) << DRF_LW_LO((o),drf))
#define DRF_LW_CLR(o,drf)   ((o)[DRF_LW_IDX((o),drf)] & ~DRF_LW_SMASK((o),drf))
#define DRF_LW_SET(o,drf,v) (DRF_LW_CLR((o),drf) | DRF_LW_VAL((o),drf,(v)))

#define DRF_HW_IDX(o,drf)   (DRF_HI(DRF_MW(drf)) / DRF_MW_SIZE(o))
#define DRF_HW_LO(o,drf)    0
#define DRF_HW_HI(o,drf)    (DRF_HI(DRF_MW(drf)) % DRF_MW_SIZE(o))
#define DRF_HW_BITS(o,drf)  (DRF_HW_HI((o),drf) - DRF_HW_LO((o),drf) + 1)
#define DRF_HW_MASK(o,drf)  (~0ULL >> (64 - DRF_HW_BITS((o),drf)))
#define DRF_HW_SMASK(o,drf) (DRF_HW_MASK((o),drf) << DRF_HW_LO((o),drf))
#define DRF_HW_GET(o,drf)   ((o)[DRF_HW_IDX(o,drf)] & DRF_HW_SMASK((o),drf))
#define DRF_HW_VAL(o,drf,v) (((long long)(v) >> DRF_LW_BITS((o),drf)) & DRF_HW_SMASK((o),drf))
#define DRF_HW_CLR(o,drf)   ((o)[DRF_HW_IDX((o),drf)] & ~DRF_HW_SMASK((o),drf))
#define DRF_HW_SET(o,drf,v) (DRF_HW_CLR((o),drf) | DRF_HW_VAL((o),drf,(v)))

/* DRF accessors. */
#define NVVAL_X(drf,v) (((v) & DRF_MASK(drf)) << DRF_LO(drf))
#define NVVAL_N(X,d,r,f,  v) NVVAL_X(d##_##r##_##f, (v))
#define NVVAL_I(X,d,r,f,i,v) NVVAL_X(d##_##r##_##f(i), (v))
#define NVVAL_(X,_1,_2,_3,_4,_5,IMPL,...) IMPL
#define NVVAL(A...) NVVAL_(X, ##A, NVVAL_I, NVVAL_N)(X, ##A)

#define NVDEF_N(X,d,r,f,  v) NVVAL_X(d##_##r##_##f, d##_##r##_##f##_##v)
#define NVDEF_I(X,d,r,f,i,v) NVVAL_X(d##_##r##_##f(i), d##_##r##_##f##_##v)
#define NVDEF_(X,_1,_2,_3,_4,_5,IMPL,...) IMPL
#define NVDEF(A...) NVDEF_(X, ##A, NVDEF_I, NVDEF_N)(X, ##A)

#define NVVAL_GET_X(o,drf) (((o) >> DRF_LO(drf)) & DRF_MASK(drf))
#define NVVAL_GET_N(X,o,d,r,f  ) NVVAL_GET_X(o, d##_##r##_##f)
#define NVVAL_GET_I(X,o,d,r,f,i) NVVAL_GET_X(o, d##_##r##_##f(i))
#define NVVAL_GET_(X,_1,_2,_3,_4,_5,IMPL,...) IMPL
#define NVVAL_GET(A...) NVVAL_GET_(X, ##A, NVVAL_GET_I, NVVAL_GET_N)(X, ##A)

#define NVVAL_TEST_X(o,drf,cmp,drfv) (NVVAL_GET_X((o), drf) cmp drfv)
#define NVVAL_TEST_N(X,o,d,r,f,  cmp,v) NVVAL_TEST_X(o, d##_##r##_##f   , cmp, (v))
#define NVVAL_TEST_I(X,o,d,r,f,i,cmp,v) NVVAL_TEST_X(o, d##_##r##_##f(i), cmp, (v))
#define NVVAL_TEST_(X,_1,_2,_3,_4,_5,_6,_7,IMPL,...) IMPL
#define NVVAL_TEST(A...) NVVAL_TEST_(X, ##A, NVVAL_TEST_I, NVVAL_TEST_N)(X, ##A)

#define NVDEF_TEST_N(X,o,d,r,f,  cmp,v) NVVAL_TEST_X(o, d##_##r##_##f   , cmp, d##_##r##_##f##_##v)
#define NVDEF_TEST_I(X,o,d,r,f,i,cmp,v) NVVAL_TEST_X(o, d##_##r##_##f(i), cmp, d##_##r##_##f##_##v)
#define NVDEF_TEST_(X,_1,_2,_3,_4,_5,_6,_7,IMPL,...) IMPL
#define NVDEF_TEST(A...) NVDEF_TEST_(X, ##A, NVDEF_TEST_I, NVDEF_TEST_N)(X, ##A)

#define NVVAL_SET_X(o,drf,v) (((o) & ~DRF_SMASK(drf)) | NVVAL_X(drf, (v)))
#define NVVAL_SET_N(X,o,d,r,f,  v) NVVAL_SET_X(o, d##_##r##_##f, (v))
#define NVVAL_SET_I(X,o,d,r,f,i,v) NVVAL_SET_X(o, d##_##r##_##f(i), (v))
#define NVVAL_SET_(X,_1,_2,_3,_4,_5,_6,IMPL,...) IMPL
#define NVVAL_SET(A...) NVVAL_SET_(X, ##A, NVVAL_SET_I, NVVAL_SET_N)(X, ##A)

#define NVDEF_SET_N(X,o,d,r,f,  v) NVVAL_SET_X(o, d##_##r##_##f,    d##_##r##_##f##_##v)
#define NVDEF_SET_I(X,o,d,r,f,i,v) NVVAL_SET_X(o, d##_##r##_##f(i), d##_##r##_##f##_##v)
#define NVDEF_SET_(X,_1,_2,_3,_4,_5,_6,IMPL,...) IMPL
#define NVDEF_SET(A...) NVDEF_SET_(X, ##A, NVDEF_SET_I, NVDEF_SET_N)(X, ##A)

/* DRF-MW accessors. */
#define NVVAL_MW_GET_X(o,drf)                                                       \
	((DRF_MW_SPANS((o),drf) ?                                                   \
	  (DRF_HW_GET((o),drf) << DRF_LW_BITS((o),drf)) : 0) | DRF_LW_GET((o),drf))
#define NVVAL_MW_GET_N(X,o,d,r,f  ) NVVAL_MW_GET_X((o), d##_##r##_##f)
#define NVVAL_MW_GET_I(X,o,d,r,f,i) NVVAL_MW_GET_X((o), d##_##r##_##f(i))
#define NVVAL_MW_GET_(X,_1,_2,_3,_4,_5,IMPL,...) IMPL
#define NVVAL_MW_GET(A...) NVVAL_MW_GET_(X, ##A, NVVAL_MW_GET_I, NVVAL_MW_GET_N)(X, ##A)

#define NVVAL_MW_SET_X(o,drf,v) do {                                           \
	(o)[DRF_LW_IDX((o),drf)] = DRF_LW_SET((o),drf,(v));                    \
	if (DRF_MW_SPANS((o),drf))                                             \
		(o)[DRF_HW_IDX((o),drf)] = DRF_HW_SET((o),drf,(v));            \
} while(0)
#define NVVAL_MW_SET_N(X,o,d,r,f,  v) NVVAL_MW_SET_X((o), d##_##r##_##f, (v))
#define NVVAL_MW_SET_I(X,o,d,r,f,i,v) NVVAL_MW_SET_X((o), d##_##r##_##f(i), (v))
#define NVVAL_MW_SET_(X,_1,_2,_3,_4,_5,_6,IMPL,...) IMPL
#define NVVAL_MW_SET(A...) NVVAL_MW_SET_(X, ##A, NVVAL_MW_SET_I, NVVAL_MW_SET_N)(X, ##A)

#define NVDEF_MW_SET_N(X,o,d,r,f,  v) NVVAL_MW_SET_X(o, d##_##r##_##f,    d##_##r##_##f##_##v)
#define NVDEF_MW_SET_I(X,o,d,r,f,i,v) NVVAL_MW_SET_X(o, d##_##r##_##f(i), d##_##r##_##f##_##v)
#define NVDEF_MW_SET_(X,_1,_2,_3,_4,_5,_6,IMPL,...) IMPL
#define NVDEF_MW_SET(A...) NVDEF_MW_SET_(X, ##A, NVDEF_MW_SET_I, NVDEF_MW_SET_N)(X, ##A)

/* Helper for reading an arbitrary object. */
#define DRF_RD_X(e,p,o,dr) e((p), (o), dr)
#define DRF_RD_N(X,e,p,o,d,r  ) DRF_RD_X(e, (p), (o), d##_##r)
#define DRF_RD_I(X,e,p,o,d,r,i) DRF_RD_X(e, (p), (o), d##_##r(i))
#define DRF_RD_(X,_1,_2,_3,_4,_5,_6,IMPL,...) IMPL
#define DRF_RD(A...) DRF_RD_(X, ##A, DRF_RD_I, DRF_RD_N)(X, ##A)

/* Helper for writing an arbitrary object. */
#define DRF_WR_X(e,p,o,dr,v) e((p), (o), dr, (v))
#define DRF_WR_N(X,e,p,o,d,r,  v) DRF_WR_X(e, (p), (o), d##_##r   , (v))
#define DRF_WR_I(X,e,p,o,d,r,i,v) DRF_WR_X(e, (p), (o), d##_##r(i), (v))
#define DRF_WR_(X,_1,_2,_3,_4,_5,_6,_7,IMPL,...) IMPL
#define DRF_WR(A...) DRF_WR_(X, ##A, DRF_WR_I, DRF_WR_N)(X, ##A)

/* Helper for modifying an arbitrary object. */
#define DRF_MR_X(er,ew,ty,p,o,dr,m,v) ({               \
	ty _t = DRF_RD_X(er, (p), (o), dr);            \
	DRF_WR_X(ew, (p), (o), dr, (_t & ~(m)) | (v)); \
	_t;                                            \
})
#define DRF_MR_N(X,er,ew,ty,p,o,d,r  ,m,v) DRF_MR_X(er, ew, ty, (p), (o), d##_##r   , (m), (v))
#define DRF_MR_I(X,er,ew,ty,p,o,d,r,i,m,v) DRF_MR_X(er, ew, ty, (p), (o), d##_##r(i), (m), (v))
#define DRF_MR_(X,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,IMPL,...) IMPL
#define DRF_MR(A...) DRF_MR_(X, ##A, DRF_MR_I, DRF_MR_N)(X, ##A)

/* Helper for extracting a field value from arbitrary object. */
#define DRF_RV_X(e,p,o,dr,drf) NVVAL_GET_X(DRF_RD_X(e, (p), (o), dr), drf)
#define DRF_RV_N(X,e,p,o,d,r,  f) DRF_RV_X(e, (p), (o), d##_##r   , d##_##r##_##f)
#define DRF_RV_I(X,e,p,o,d,r,i,f) DRF_RV_X(e, (p), (o), d##_##r(i), d##_##r##_##f)
#define DRF_RV_(X,_1,_2,_3,_4,_5,_6,_7,IMPL,...) IMPL
#define DRF_RV(A...) DRF_RV_(X, ##A, DRF_RV_I, DRF_RV_N)(X, ##A)

/* Helper for writing field value to arbitrary object (all other bits cleared). */
#define DRF_WV_N(X,e,p,o,d,r,  f,v)                                    \
	DRF_WR_X(e, (p), (o), d##_##r   , NVVAL_X(d##_##r##_##f, (v)))
#define DRF_WV_I(X,e,p,o,d,r,i,f,v)                                    \
	DRF_WR_X(e, (p), (o), d##_##r(i), NVVAL_X(d##_##r##_##f, (v)))
#define DRF_WV_(X,_1,_2,_3,_4,_5,_6,_7,_8,IMPL,...) IMPL
#define DRF_WV(A...) DRF_WV_(X, ##A, DRF_WV_I, DRF_WV_N)(X, ##A)

/* Helper for writing field definition to arbitrary object (all other bits cleared). */
#define DRF_WD_N(X,e,p,o,d,r,  f,v)                                                    \
	DRF_WR_X(e, (p), (o), d##_##r   , NVVAL_X(d##_##r##_##f, d##_##r##_##f##_##v))
#define DRF_WD_I(X,e,p,o,d,r,i,f,v)                                                    \
	DRF_WR_X(e, (p), (o), d##_##r(i), NVVAL_X(d##_##r##_##f, d##_##r##_##f##_##v))
#define DRF_WD_(X,_1,_2,_3,_4,_5,_6,_7,_8,IMPL,...) IMPL
#define DRF_WD(A...) DRF_WD_(X, ##A, DRF_WD_I, DRF_WD_N)(X, ##A)

/* Helper for modifying field value in arbitrary object. */
#define DRF_MV_N(X,er,ew,ty,p,o,d,r,  f,v)                                               \
	NVVAL_GET_X(DRF_MR_X(er, ew, ty, (p), (o), d##_##r   , DRF_SMASK(d##_##r##_##f), \
		    NVVAL_X(d##_##r##_##f, (v))), d##_##r##_##f)
#define DRF_MV_I(X,er,ew,ty,p,o,d,r,i,f,v)                                               \
	NVVAL_GET_X(DRF_MR_X(er, ew, ty, (p), (o), d##_##r(i), DRF_SMASK(d##_##r##_##f), \
		    NVVAL_X(d##_##r##_##f, (v))), d##_##r##_##f)
#define DRF_MV_(X,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,IMPL,...) IMPL
#define DRF_MV(A...) DRF_MV_(X, ##A, DRF_MV_I, DRF_MV_N)(X, ##A)

/* Helper for modifying field definition in arbitrary object. */
#define DRF_MD_N(X,er,ew,ty,p,o,d,r,  f,v)                                               \
	NVVAL_GET_X(DRF_MR_X(er, ew, ty, (p), (o), d##_##r   , DRF_SMASK(d##_##r##_##f), \
		    NVVAL_X(d##_##r##_##f, d##_##r##_##f##_##v)), d##_##r##_##f)
#define DRF_MD_I(X,er,ew,ty,p,o,d,r,i,f,v)                                               \
	NVVAL_GET_X(DRF_MR_X(er, ew, ty, (p), (o), d##_##r(i), DRF_SMASK(d##_##r##_##f), \
		    NVVAL_X(d##_##r##_##f, d##_##r##_##f##_##v)), d##_##r##_##f)
#define DRF_MD_(X,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,IMPL,...) IMPL
#define DRF_MD(A...) DRF_MD_(X, ##A, DRF_MD_I, DRF_MD_N)(X, ##A)

/* Helper for testing against field value in aribtrary object */
#define DRF_TV_N(X,e,p,o,d,r,  f,cmp,v)                                          \
	NVVAL_TEST_X(DRF_RD_X(e, (p), (o), d##_##r   ), d##_##r##_##f, cmp, (v))
#define DRF_TV_I(X,e,p,o,d,r,i,f,cmp,v)                                          \
	NVVAL_TEST_X(DRF_RD_X(e, (p), (o), d##_##r(i)), d##_##r##_##f, cmp, (v))
#define DRF_TV_(X,_1,_2,_3,_4,_5,_6,_7,_8,_9,IMPL,...) IMPL
#define DRF_TV(A...) DRF_TV_(X, ##A, DRF_TV_I, DRF_TV_N)(X, ##A)

/* Helper for testing against field definition in aribtrary object */
#define DRF_TD_N(X,e,p,o,d,r,  f,cmp,v)                                                          \
	NVVAL_TEST_X(DRF_RD_X(e, (p), (o), d##_##r   ), d##_##r##_##f, cmp, d##_##r##_##f##_##v)
#define DRF_TD_I(X,e,p,o,d,r,i,f,cmp,v)                                                          \
	NVVAL_TEST_X(DRF_RD_X(e, (p), (o), d##_##r(i)), d##_##r##_##f, cmp, d##_##r##_##f##_##v)
#define DRF_TD_(X,_1,_2,_3,_4,_5,_6,_7,_8,_9,IMPL,...) IMPL
#define DRF_TD(A...) DRF_TD_(X, ##A, DRF_TD_I, DRF_TD_N)(X, ##A)
#endif
