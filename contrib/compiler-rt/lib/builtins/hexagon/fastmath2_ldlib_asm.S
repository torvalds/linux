//===----------------------Hexagon builtin routine ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/* ==================================================================== *

fast2_QLDOUBLE fast2_ldadd(fast2_QLDOUBLE a,fast2_QLDOUBLE b) {
      fast2_QLDOUBLE c;
      lint manta = a & MANTMASK;
      int  expa  = Q6_R_sxth_R(a) ;
      lint mantb = b & MANTMASK;
      int  expb  = Q6_R_sxth_R(b) ;
      int  exp, expdiff, j, k, hi, lo, cn;
      lint mant;

        expdiff = (int) Q6_P_vabsdiffh_PP(a, b);
        expdiff = Q6_R_sxth_R(expdiff) ;
        if (expdiff > 63) { expdiff = 62;}
        if (expa > expb) {
          exp = expa + 1;
          expa = 1;
          expb = expdiff + 1;
        } else {
          exp = expb + 1;
          expb = 1;
          expa = expdiff + 1;
        }
        mant = (manta>>expa) + (mantb>>expb);

        hi = (int) (mant>>32);
        lo = (int) (mant);

        k =  Q6_R_normamt_R(hi);
        if(hi == 0 || hi == -1) k = 31+Q6_R_normamt_R(lo);

        mant = (mant << k);
        cn  = (mant == 0x8000000000000000LL);
        exp = exp - k + cn;

        if (mant ==  0 || mant == -1)  exp = 0x8001;
        c = (mant & MANTMASK) | (((lint) exp) & EXP_MASK);
      return(c);
 }
 * ==================================================================== */
        .text
        .global fast2_ldadd_asm
        .type fast2_ldadd_asm, @function
fast2_ldadd_asm:
#define manta      R1:0
#define lmanta     R1:0
#define mantb      R3:2
#define lmantb     R3:2
#define expa       R4
#define expb       R5
#define expd       R6
#define exp        R8
#define c63        R9
#define lmant      R1:0
#define k          R4
#define ce         P0
#define zero       R3:2
        .falign
      {
        expa = memw(r29+#8)
        expb = memw(r29+#24)
        r7 = r0
      }
      {
        expd = sub(expa, expb):sat
        ce = CMP.GT(expa, expb);
        if ( ce.new) exp = add(expa, #1)
        if (!ce.new) exp = add(expb, #1)
      } {
        expd = abs(expd):sat
        if ( ce) expa = #1
        if (!ce) expb = #1
        c63 = #62
      } {
        expd = MIN(expd, c63)
        manta = memd(r29+#0)
        mantb = memd(r29+#16)
      } {
        if (!ce) expa = add(expd, #1)
        if ( ce) expb = add(expd, #1)
      } {
        lmanta = ASR(lmanta, expa)
        lmantb = ASR(lmantb, expb)
      } {
        lmant = add(lmanta, lmantb)
        zero = #0
      } {
        k  = clb(lmant)
        c63.L =#0x0001
      } {
        exp -= add(k, #-1)  //exp =  exp - (k-1)
        k = add(k, #-1)
        p0 = cmp.gt(k, #58)
        c63.H =#0x8000
      } {
        if(!p0)memw(r7+#8) = exp
        lmant = ASL(lmant, k)
        if(p0) jump .Ldenorma
      } {
        memd(r7+#0) = lmant
        jumpr  r31
      }
.Ldenorma:
        memd(r7+#0) = zero
      {
        memw(r7+#8) = c63
        jumpr  r31
      }
/* =================================================================== *
 fast2_QLDOUBLE fast2_ldsub(fast2_QLDOUBLE a,fast2_QLDOUBLE b) {
      fast2_QLDOUBLE c;
      lint manta = a & MANTMASK;
      int  expa  = Q6_R_sxth_R(a) ;
      lint mantb = b & MANTMASK;
      int  expb  = Q6_R_sxth_R(b) ;
      int  exp, expdiff, j, k;
      lint mant;

        expdiff = (int) Q6_P_vabsdiffh_PP(a, b);
        expdiff = Q6_R_sxth_R(expdiff) ;
        if (expdiff > 63) { expdiff = 62;}
        if (expa > expb) {
          exp = expa + 1;
          expa = 1;
          expb = expdiff + 1;
        } else {
          exp = expb + 1;
          expb = 1;
          expa = expdiff + 1;
        }
        mant = (manta>>expa) - (mantb>>expb);
        k =  Q6_R_clb_P(mant)-1;
        mant = (mant << k);
        exp = exp - k;
        if (mant ==  0 || mant == -1)  exp = 0x8001;
        c = (mant & MANTMASK) | (((lint) exp) & EXP_MASK);
      return(c);
 }
 * ==================================================================== */
        .text
        .global fast2_ldsub_asm
        .type fast2_ldsub_asm, @function
fast2_ldsub_asm:
#define manta      R1:0
#define lmanta     R1:0
#define mantb      R3:2
#define lmantb     R3:2
#define expa       R4
#define expb       R5
#define expd       R6
#define exp        R8
#define c63        R9
#define lmant      R1:0
#define k          R4
#define ce         P0
#define zero       R3:2
        .falign
      {
        expa = memw(r29+#8)
        expb = memw(r29+#24)
        r7 = r0
      }
      {
        expd = sub(expa, expb):sat
        ce = CMP.GT(expa, expb);
        if ( ce.new) exp = add(expa, #1)
        if (!ce.new) exp = add(expb, #1)
      } {
        expd = abs(expd):sat
        if ( ce) expa = #1
        if (!ce) expb = #1
        c63 = #62
      } {
        expd = min(expd, c63)
        manta = memd(r29+#0)
        mantb = memd(r29+#16)
      } {
        if (!ce) expa = add(expd, #1)
        if ( ce) expb = add(expd, #1)
      } {
        lmanta = ASR(lmanta, expa)
        lmantb = ASR(lmantb, expb)
      } {
        lmant = sub(lmanta, lmantb)
        zero = #0
      } {
        k  = clb(lmant)
        c63.L =#0x0001
      } {
        exp -= add(k, #-1)  //exp =  exp - (k+1)
        k = add(k, #-1)
        p0 = cmp.gt(k, #58)
        c63.H =#0x8000
      } {
        if(!p0)memw(r7+#8) = exp
        lmant = asl(lmant, k)
        if(p0) jump .Ldenorma_s
      } {
        memd(r7+#0) = lmant
        jumpr  r31
      }
.Ldenorma_s:
        memd(r7+#0) = zero
      {
        memw(r7+#8) = c63
        jumpr  r31
      }

/* ==================================================================== *
 fast2_QLDOUBLE fast2_ldmpy(fast2_QLDOUBLE a,fast2_QLDOUBLE b) {
        fast2_QLDOUBLE c;
        lint manta = a & MANTMASK;
        int  expa  = Q6_R_sxth_R(a) ;
        lint mantb = b & MANTMASK;
        int  expb  = Q6_R_sxth_R(b) ;
        int exp, k;
        lint mant;
        int          hia, hib, hi, lo;
        unsigned int loa, lob;

        hia = (int)(a >> 32);
        loa = Q6_R_extractu_RII((int)manta, 31, 1);
        hib = (int)(b >> 32);
        lob = Q6_R_extractu_RII((int)mantb, 31, 1);

        mant = Q6_P_mpy_RR(hia, lob);
        mant = Q6_P_mpyacc_RR(mant,hib, loa);
        mant = (mant >> 30) + (Q6_P_mpy_RR(hia, hib)<<1);

        hi = (int) (mant>>32);

        k =  Q6_R_normamt_R(hi);
        mant = mant << k;
        exp = expa + expb - k;
        if (mant ==  0 || mant == -1)  exp = 0x8001;
        c = (mant & MANTMASK) | (((lint) exp) & EXP_MASK);
        return(c);
 }
 * ==================================================================== */
        .text
        .global fast2_ldmpy_asm
        .type fast2_ldmpy_asm, @function
fast2_ldmpy_asm:

#define mantxl_    R9
#define mantxl     R14
#define mantxh     R15
#define mantx      R15:14
#define mantbl     R2
#define mantbl_    R8
#define mantbh     R3
#define mantb      R3:2
#define expa       R4
#define expb       R5
#define c8001      R8
#define mantd      R7:6
#define lmantc     R11:10
#define kp         R9
#define min        R13:12
#define minh       R13
#define max        R13:12
#define maxh       R13
#define ret        R0

        .falign
      {
        mantx = memd(r29+#0)
        mantb = memd(r29+#16)
        min = #0
      }
      {
        mantbl_= extractu(mantbl, #31, #1)
        mantxl_= extractu(mantxl, #31, #1)
        minh.H = #0x8000
      }
      {
        lmantc = mpy(mantxh, mantbh)
        mantd = mpy(mantxh, mantbl_)
        expa = memw(r29+#8)
        expb = memw(r29+#24)
      }
      {
        lmantc = add(lmantc, lmantc)
        mantd += mpy(mantbh, mantxl_)
      }
      {
        mantd = asr(mantd, #30)
        c8001.L =  #0x0001
        p1 = cmp.eq(mantx, mantb)
      }
      {
        mantd = add(mantd, lmantc)
        expa= add(expa, expb)
        p2 = cmp.eq(mantb, min)
      }
      {
        kp  = clb(mantd)
        c8001.H =  #0x8000
        p1 = and(p1, p2)
      }
      {
        expa-= add(kp, #-1)
        kp = add(kp, #-1)
        if(p1) jump .Lsat
      }
      {
        mantd = asl(mantd, kp)
        memw(ret+#8) = expa
	p0 = cmp.gt(kp, #58)
        if(p0.new) jump:NT .Ldenorm   //rarely happens
      }
      {
        memd(ret+#0) = mantd
        jumpr  r31
      }
.Lsat:
      {
        max = #0
        expa+= add(kp, #1)
      }
      {
        maxh.H = #0x4000
        memw(ret+#8) = expa
      }
      {
        memd(ret+#0) = max
        jumpr  r31
      }
.Ldenorm:
      {
        memw(ret+#8) = c8001
        mantx = #0
      }
      {
        memd(ret+#0) = mantx
        jumpr  r31
      }
