#ifndef _FPU_PROTO_H
#define _FPU_PROTO_H

/* errors.c */
extern void FPU_illegal(void);
extern void FPU_printall(void);
asmlinkage void FPU_exception(int n);
extern int real_1op_NaN(FPU_REG *a);
extern int real_2op_NaN(FPU_REG const *b, u_char tagb, int deststnr,
			FPU_REG const *defaultNaN);
asmlinkage int arith_invalid(int deststnr);
asmlinkage int FPU_divide_by_zero(int deststnr, u_char sign);
extern int set_precision_flag(int flags);
asmlinkage void set_precision_flag_up(void);
asmlinkage void set_precision_flag_down(void);
asmlinkage int denormal_operand(void);
asmlinkage int arith_overflow(FPU_REG *dest);
asmlinkage int arith_underflow(FPU_REG *dest);
extern void FPU_stack_overflow(void);
extern void FPU_stack_underflow(void);
extern void FPU_stack_underflow_i(int i);
extern void FPU_stack_underflow_pop(int i);
/* fpu_arith.c */
extern void fadd__(void);
extern void fmul__(void);
extern void fsub__(void);
extern void fsubr_(void);
extern void fdiv__(void);
extern void fdivr_(void);
extern void fadd_i(void);
extern void fmul_i(void);
extern void fsubri(void);
extern void fsub_i(void);
extern void fdivri(void);
extern void fdiv_i(void);
extern void faddp_(void);
extern void fmulp_(void);
extern void fsubrp(void);
extern void fsubp_(void);
extern void fdivrp(void);
extern void fdivp_(void);
/* fpu_aux.c */
extern void finit(void);
extern void finit_(void);
extern void fstsw_(void);
extern void fp_nop(void);
extern void fld_i_(void);
extern void fxch_i(void);
extern void ffree_(void);
extern void ffreep(void);
extern void fst_i_(void);
extern void fstp_i(void);
/* fpu_entry.c */
extern void math_emulate(struct math_emu_info *info);
extern void math_abort(struct math_emu_info *info, unsigned int signal);
/* fpu_etc.c */
extern void FPU_etc(void);
/* fpu_tags.c */
extern int FPU_gettag0(void);
extern int FPU_gettagi(int stnr);
extern int FPU_gettag(int regnr);
extern void FPU_settag0(int tag);
extern void FPU_settagi(int stnr, int tag);
extern void FPU_settag(int regnr, int tag);
extern int FPU_Special(FPU_REG const *ptr);
extern int isNaN(FPU_REG const *ptr);
extern void FPU_pop(void);
extern int FPU_empty_i(int stnr);
extern int FPU_stackoverflow(FPU_REG ** st_new_ptr);
extern void FPU_copy_to_regi(FPU_REG const *r, u_char tag, int stnr);
extern void FPU_copy_to_reg1(FPU_REG const *r, u_char tag);
extern void FPU_copy_to_reg0(FPU_REG const *r, u_char tag);
/* fpu_trig.c */
extern void FPU_triga(void);
extern void FPU_trigb(void);
/* get_address.c */
extern void __user *FPU_get_address(u_char FPU_modrm, unsigned long *fpu_eip,
				    struct address *addr,
				    fpu_addr_modes addr_modes);
extern void __user *FPU_get_address_16(u_char FPU_modrm, unsigned long *fpu_eip,
				       struct address *addr,
				       fpu_addr_modes addr_modes);
/* load_store.c */
extern int FPU_load_store(u_char type, fpu_addr_modes addr_modes,
			  void __user * data_address);
/* poly_2xm1.c */
extern int poly_2xm1(u_char sign, FPU_REG * arg, FPU_REG *result);
/* poly_atan.c */
extern void poly_atan(FPU_REG * st0_ptr, u_char st0_tag, FPU_REG *st1_ptr,
		      u_char st1_tag);
/* poly_l2.c */
extern void poly_l2(FPU_REG *st0_ptr, FPU_REG *st1_ptr, u_char st1_sign);
extern int poly_l2p1(u_char s0, u_char s1, FPU_REG *r0, FPU_REG *r1,
		     FPU_REG * d);
/* poly_sin.c */
extern void poly_sine(FPU_REG *st0_ptr);
extern void poly_cos(FPU_REG *st0_ptr);
/* poly_tan.c */
extern void poly_tan(FPU_REG *st0_ptr);
/* reg_add_sub.c */
extern int FPU_add(FPU_REG const *b, u_char tagb, int destrnr, int control_w);
extern int FPU_sub(int flags, int rm, int control_w);
/* reg_compare.c */
extern int FPU_compare_st_data(FPU_REG const *loaded_data, u_char loaded_tag);
extern void fcom_st(void);
extern void fcompst(void);
extern void fcompp(void);
extern void fucom_(void);
extern void fucomp(void);
extern void fucompp(void);
/* reg_constant.c */
extern void fconst(void);
/* reg_ld_str.c */
extern int FPU_load_extended(long double __user *s, int stnr);
extern int FPU_load_double(double __user *dfloat, FPU_REG *loaded_data);
extern int FPU_load_single(float __user *single, FPU_REG *loaded_data);
extern int FPU_load_int64(long long __user *_s);
extern int FPU_load_int32(long __user *_s, FPU_REG *loaded_data);
extern int FPU_load_int16(short __user *_s, FPU_REG *loaded_data);
extern int FPU_load_bcd(u_char __user *s);
extern int FPU_store_extended(FPU_REG *st0_ptr, u_char st0_tag,
			      long double __user * d);
extern int FPU_store_double(FPU_REG *st0_ptr, u_char st0_tag,
			    double __user * dfloat);
extern int FPU_store_single(FPU_REG *st0_ptr, u_char st0_tag,
			    float __user * single);
extern int FPU_store_int64(FPU_REG *st0_ptr, u_char st0_tag,
			   long long __user * d);
extern int FPU_store_int32(FPU_REG *st0_ptr, u_char st0_tag, long __user *d);
extern int FPU_store_int16(FPU_REG *st0_ptr, u_char st0_tag, short __user *d);
extern int FPU_store_bcd(FPU_REG *st0_ptr, u_char st0_tag, u_char __user *d);
extern int FPU_round_to_int(FPU_REG *r, u_char tag);
extern u_char __user *fldenv(fpu_addr_modes addr_modes, u_char __user *s);
extern void frstor(fpu_addr_modes addr_modes, u_char __user *data_address);
extern u_char __user *fstenv(fpu_addr_modes addr_modes, u_char __user *d);
extern void fsave(fpu_addr_modes addr_modes, u_char __user *data_address);
extern int FPU_tagof(FPU_REG *ptr);
/* reg_mul.c */
extern int FPU_mul(FPU_REG const *b, u_char tagb, int deststnr, int control_w);

extern int FPU_div(int flags, int regrm, int control_w);
/* reg_convert.c */
extern int FPU_to_exp16(FPU_REG const *a, FPU_REG *x);
#endif /* _FPU_PROTO_H */
