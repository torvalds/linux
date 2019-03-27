//===----------------------Hexagon builtin routine ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/* ==================================================================== */
/*   FUNCTIONS Optimized double floating point operators                */
/* ==================================================================== */
/*      c = dadd_asm(a, b)                                              */
/* ==================================================================== *
fast2_QDOUBLE fast2_dadd(fast2_QDOUBLE a,fast2_QDOUBLE b) {
      fast2_QDOUBLE c;
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
        .global fast2_dadd_asm
        .type fast2_dadd_asm, @function
fast2_dadd_asm:
#define manta      R0
#define mantexpa   R1:0
#define lmanta     R1:0
#define mantb      R2
#define mantexpb   R3:2
#define lmantb     R3:2
#define expa       R4
#define expb       R5
#define mantexpd   R7:6
#define expd       R6
#define exp        R8
#define c63        R9
#define lmant      R1:0
#define manth      R1
#define mantl      R0
#define minmin     R11:10  // exactly 0x000000000000008001LL
#define minminl    R10
#define k          R4
#define ce         P0
        .falign
      {
        mantexpd = VABSDIFFH(mantexpa, mantexpb) //represented as 0x08001LL
        c63 = #62
        expa = SXTH(manta)
        expb = SXTH(mantb)
      } {
        expd = SXTH(expd)
        ce = CMP.GT(expa, expb);
        if ( ce.new) exp = add(expa, #1)
        if (!ce.new) exp = add(expb, #1)
      } {
        if ( ce) expa = #1
        if (!ce) expb = #1
        manta.L = #0
        expd = MIN(expd, c63)
      } {
        if (!ce) expa = add(expd, #1)
        if ( ce) expb = add(expd, #1)
        mantb.L = #0
        minmin = #0
      } {
        lmanta = ASR(lmanta, expa)
        lmantb = ASR(lmantb, expb)
      } {
        lmant = add(lmanta, lmantb)
        minminl.L = #0x8001
      } {
        k  = clb(lmant)
        c63 = #58
      } {
        k = add(k, #-1)
        p0 = cmp.gt(k, c63)
      } {
        mantexpa = ASL(lmant, k)
        exp = SUB(exp, k)
        if(p0) jump .Ldenorma
      } {
        manta = insert(exp, #16, #0)
        jumpr  r31
      }
.Ldenorma:
      {
        mantexpa = minmin
        jumpr  r31
      }
/* =================================================================== *
 fast2_QDOUBLE fast2_dsub(fast2_QDOUBLE a,fast2_QDOUBLE b) {
      fast2_QDOUBLE c;
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
        .global fast2_dsub_asm
        .type fast2_dsub_asm, @function
fast2_dsub_asm:

#define manta      R0
#define mantexpa   R1:0
#define lmanta     R1:0
#define mantb      R2
#define mantexpb   R3:2
#define lmantb     R3:2
#define expa       R4
#define expb       R5
#define mantexpd   R7:6
#define expd       R6
#define exp        R8
#define c63        R9
#define lmant      R1:0
#define manth      R1
#define mantl      R0
#define minmin     R11:10  // exactly 0x000000000000008001LL
#define minminl    R10
#define k          R4
#define ce         P0
        .falign
      {
        mantexpd = VABSDIFFH(mantexpa, mantexpb) //represented as 0x08001LL
        c63 = #62
        expa = SXTH(manta)
        expb = SXTH(mantb)
      } {
        expd = SXTH(expd)
        ce = CMP.GT(expa, expb);
        if ( ce.new) exp = add(expa, #1)
        if (!ce.new) exp = add(expb, #1)
      } {
        if ( ce) expa = #1
        if (!ce) expb = #1
        manta.L = #0
        expd = MIN(expd, c63)
      } {
        if (!ce) expa = add(expd, #1)
        if ( ce) expb = add(expd, #1)
        mantb.L = #0
        minmin = #0
      } {
        lmanta = ASR(lmanta, expa)
        lmantb = ASR(lmantb, expb)
      } {
        lmant = sub(lmanta, lmantb)
        minminl.L = #0x8001
      } {
        k  = clb(lmant)
        c63 = #58
      } {
        k = add(k, #-1)
        p0 = cmp.gt(k, c63)
      } {
        mantexpa = ASL(lmant, k)
        exp = SUB(exp, k)
        if(p0) jump .Ldenorm
      } {
        manta = insert(exp, #16, #0)
        jumpr  r31
      }
.Ldenorm:
      {
        mantexpa = minmin
        jumpr  r31
      }
/* ==================================================================== *
 fast2_QDOUBLE fast2_dmpy(fast2_QDOUBLE a,fast2_QDOUBLE b) {
        fast2_QDOUBLE c;
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
        .global fast2_dmpy_asm
        .type fast2_dmpy_asm, @function
fast2_dmpy_asm:

#define mantal     R0
#define mantah     R1
#define mantexpa   R1:0
#define mantbl     R2
#define mantbh     R3
#define mantexpb   R3:2
#define expa       R4
#define expb       R5
#define c8001      R12
#define mantexpd   R7:6
#define mantdh     R7
#define exp        R8
#define lmantc     R11:10
#define kb         R9
#define guard      R11
#define mantal_    R12
#define mantbl_    R13
#define min        R15:14
#define minh       R15

        .falign
      {
        mantbl_= lsr(mantbl, #16)
        expb = sxth(mantbl)
        expa = sxth(mantal)
        mantal_= lsr(mantal, #16)
      }
      {
        lmantc = mpy(mantah, mantbh)
        mantexpd = mpy(mantah, mantbl_)
        mantal.L = #0x0
        min = #0
      }
      {
        lmantc = add(lmantc, lmantc)
        mantexpd+= mpy(mantbh, mantal_)
        mantbl.L = #0x0
        minh.H = #0x8000
      }
      {
        mantexpd = asr(mantexpd, #15)
        c8001.L =  #0x8001
        p1 = cmp.eq(mantexpa, mantexpb)
      }
      {
        mantexpd = add(mantexpd, lmantc)
        exp = add(expa, expb)
        p2 = cmp.eq(mantexpa, min)
      }
      {
        kb  = clb(mantexpd)
        mantexpb = abs(mantexpd)
        guard = #58
      }
      {
        p1 = and(p1, p2)
        exp = sub(exp, kb)
        kb = add(kb, #-1)
	p0 = cmp.gt(kb, guard)
      }
      {
        exp = add(exp, #1)
        mantexpa = asl(mantexpd, kb)
        if(p1) jump .Lsat   //rarely happens
      }
      {
        mantal = insert(exp,#16, #0)
        if(!p0) jumpr  r31
      }
      {
        mantal = insert(c8001,#16, #0)
        jumpr  r31
      }
.Lsat:
      {
        mantexpa = #-1
      }
      {
        mantexpa = lsr(mantexpa, #1)
      }
      {
        mantal = insert(exp,#16, #0)
        jumpr  r31
      }

/* ==================================================================== *
 int fast2_qd2f(fast2_QDOUBLE a) {
        int exp;
        long long int manta;
        int ic, rnd, mantb;

        manta = a>>32;
        exp = Q6_R_sxth_R(a) ;
        ic = 0x80000000 & manta;
        manta = Q6_R_abs_R_sat(manta);
        mantb = (manta + rnd)>>7;
        rnd = 0x40
        exp = (exp + 126);
        if((manta & 0xff) == rnd) rnd = 0x00;
        if((manta & 0x7fffffc0) == 0x7fffffc0) {
           manta = 0x0; exp++;
        } else {
           manta= mantb & 0x007fffff;
        }
        exp = (exp << 23) & 0x7fffffc0;
        ic = Q6_R_addacc_RR(ic, exp, manta);
        return (ic);
 }
 * ==================================================================== */

        .text
        .global fast2_qd2f_asm
        .type fast2_qd2f_asm, @function
fast2_qd2f_asm:
#define mantah   R1
#define mantal   R0
#define cff      R0
#define mant     R3
#define expo     R4
#define rnd      R5
#define mask     R6
#define c07f     R7
#define c80      R0
#define mantb    R2
#define ic       R0

      .falign
     {
       mant = abs(mantah):sat
       expo = sxth(mantal)
       rnd = #0x40
       mask.L = #0xffc0
     }
     {
       cff = extractu(mant, #8, #0)
       p2 = cmp.gt(expo, #126)
       p3 = cmp.ge(expo, #-126)
       mask.H = #0x7fff
     }
     {
       p1 = cmp.eq(cff,#0x40)
       if(p1.new) rnd = #0
       expo = add(expo, #126)
       if(!p3) jump .Lmin
     }
     {
       p0 = bitsset(mant, mask)
       c80.L = #0x0000
       mantb = add(mant, rnd)
       c07f = lsr(mask, #8)
     }
     {
       if(p0) expo = add(expo, #1)
       if(p0) mant = #0
       mantb = lsr(mantb, #7)
       c80.H = #0x8000
     }
     {
       ic = and(c80, mantah)
       mask &= asl(expo, #23)
       if(!p0) mant = and(mantb, c07f)
       if(p2) jump .Lmax
     }
     {
       ic += add(mask, mant)
       jumpr r31
     }
.Lmax:
     {
       ic.L = #0xffff;
     }
     {
       ic.H = #0x7f7f;
       jumpr r31
     }
.Lmin:
     {
       ic = #0x0
       jumpr r31
     }

/* ==================================================================== *
fast2_QDOUBLE fast2_f2qd(int ia) {
        lint exp;
        lint mant;
        fast2_QDOUBLE c;

        mant = ((ia << 7) | 0x40000000)&0x7fffff80 ;
        if (ia & 0x80000000) mant = -mant;
        exp =  ((ia >> 23) & 0xFFLL) - 126;
        c = (mant<<32) | Q6_R_zxth_R(exp);;
        return(c);
}
 * ==================================================================== */
        .text
        .global fast2_f2qd_asm
        .type fast2_f2qd_asm, @function
fast2_f2qd_asm:
#define ia    R0
#define mag   R3
#define mantr R1
#define expr  R0
#define zero  R2
#define maxneg R5:4
#define maxnegl R4
        .falign
  {
       mantr = asl(ia, #7)
       p0 = tstbit(ia, #31)
       maxneg = #0
       mag = add(ia,ia)
  }
  {
       mantr = setbit(mantr, #30)
       expr= extractu(ia,#8,#23)
       maxnegl.L = #0x8001
       p1 = cmp.eq(mag, #0)
  }
  {
       mantr= extractu(mantr, #31, #0)
       expr= add(expr, #-126)
       zero = #0
       if(p1) jump .Lminqd
  }
  {
       expr = zxth(expr)
       if(p0) mantr= sub(zero, mantr)
       jumpr r31
  }
.Lminqd:
  {
       R1:0 = maxneg
       jumpr r31
  }
