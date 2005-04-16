/*
 * Code to call _setup_L2CR to flus, invalidate and disable the L2,
 * and if present, do the same to the L3.
 */

#define CLEAR_CACHES						\
	bl	_setup_L2CR;					\
								\
	/* If 745x, turn off L3CR as well */			\
	mfspr	r8,SPRN_PVR;					\
	srwi	r8,r8,16;					\
								\
	cmpli	cr0,r8,0x8000;			/* 7450 */	\
	cmpli	cr1,r8,0x8001;			/* 7455 */	\
	cmpli	cr2,r8,0x8002;			/* 7457 */	\
	/* Now test if any are true. */				\
	cror	4*cr0+eq,4*cr0+eq,4*cr1+eq;			\
	cror	4*cr0+eq,4*cr0+eq,4*cr2+eq;			\
	beql	_setup_L3CR
