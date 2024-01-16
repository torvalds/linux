|
|	srem_mod.sa 3.1 12/10/90
|
|      The entry point sMOD computes the floating point MOD of the
|      input values X and Y. The entry point sREM computes the floating
|      point (IEEE) REM of the input values X and Y.
|
|      INPUT
|      -----
|      Double-extended value Y is pointed to by address in register
|      A0. Double-extended value X is located in -12(A0). The values
|      of X and Y are both nonzero and finite; although either or both
|      of them can be denormalized. The special cases of zeros, NaNs,
|      and infinities are handled elsewhere.
|
|      OUTPUT
|      ------
|      FREM(X,Y) or FMOD(X,Y), depending on entry point.
|
|       ALGORITHM
|       ---------
|
|       Step 1.  Save and strip signs of X and Y: signX := sign(X),
|                signY := sign(Y), X := |X|, Y := |Y|,
|                signQ := signX EOR signY. Record whether MOD or REM
|                is requested.
|
|       Step 2.  Set L := expo(X)-expo(Y), k := 0, Q := 0.
|                If (L < 0) then
|                   R := X, go to Step 4.
|                else
|                   R := 2^(-L)X, j := L.
|                endif
|
|       Step 3.  Perform MOD(X,Y)
|            3.1 If R = Y, go to Step 9.
|            3.2 If R > Y, then { R := R - Y, Q := Q + 1}
|            3.3 If j = 0, go to Step 4.
|            3.4 k := k + 1, j := j - 1, Q := 2Q, R := 2R. Go to
|                Step 3.1.
|
|       Step 4.  At this point, R = X - QY = MOD(X,Y). Set
|                Last_Subtract := false (used in Step 7 below). If
|                MOD is requested, go to Step 6.
|
|       Step 5.  R = MOD(X,Y), but REM(X,Y) is requested.
|            5.1 If R < Y/2, then R = MOD(X,Y) = REM(X,Y). Go to
|                Step 6.
|            5.2 If R > Y/2, then { set Last_Subtract := true,
|                Q := Q + 1, Y := signY*Y }. Go to Step 6.
|            5.3 This is the tricky case of R = Y/2. If Q is odd,
|                then { Q := Q + 1, signX := -signX }.
|
|       Step 6.  R := signX*R.
|
|       Step 7.  If Last_Subtract = true, R := R - Y.
|
|       Step 8.  Return signQ, last 7 bits of Q, and R as required.
|
|       Step 9.  At this point, R = 2^(-j)*X - Q Y = Y. Thus,
|                X = 2^(j)*(Q+1)Y. set Q := 2^(j)*(Q+1),
|                R := 0. Return signQ, last 7 bits of Q, and R.
|
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

SREM_MOD:    |idnt    2,1 | Motorola 040 Floating Point Software Package

	|section    8

#include "fpsp.h"

	.set	Mod_Flag,L_SCR3
	.set	SignY,FP_SCR3+4
	.set	SignX,FP_SCR3+8
	.set	SignQ,FP_SCR3+12
	.set	Sc_Flag,FP_SCR4

	.set	Y,FP_SCR1
	.set	Y_Hi,Y+4
	.set	Y_Lo,Y+8

	.set	R,FP_SCR2
	.set	R_Hi,R+4
	.set	R_Lo,R+8


Scale:     .long	0x00010000,0x80000000,0x00000000,0x00000000

	|xref	t_avoid_unsupp

        .global        smod
smod:

   movel               #0,Mod_Flag(%a6)
   bras                Mod_Rem

        .global        srem
srem:

   movel               #1,Mod_Flag(%a6)

Mod_Rem:
|..Save sign of X and Y
   moveml              %d2-%d7,-(%a7)     | ...save data registers
   movew               (%a0),%d3
   movew               %d3,SignY(%a6)
   andil               #0x00007FFF,%d3   | ...Y := |Y|

|
   movel               4(%a0),%d4
   movel               8(%a0),%d5        | ...(D3,D4,D5) is |Y|

   tstl                %d3
   bnes                Y_Normal

   movel               #0x00003FFE,%d3	| ...$3FFD + 1
   tstl                %d4
   bnes                HiY_not0

HiY_0:
   movel               %d5,%d4
   clrl                %d5
   subil               #32,%d3
   clrl                %d6
   bfffo                %d4{#0:#32},%d6
   lsll                %d6,%d4
   subl                %d6,%d3           | ...(D3,D4,D5) is normalized
|                                       ...with bias $7FFD
   bras                Chk_X

HiY_not0:
   clrl                %d6
   bfffo                %d4{#0:#32},%d6
   subl                %d6,%d3
   lsll                %d6,%d4
   movel               %d5,%d7           | ...a copy of D5
   lsll                %d6,%d5
   negl                %d6
   addil               #32,%d6
   lsrl                %d6,%d7
   orl                 %d7,%d4           | ...(D3,D4,D5) normalized
|                                       ...with bias $7FFD
   bras                Chk_X

Y_Normal:
   addil               #0x00003FFE,%d3   | ...(D3,D4,D5) normalized
|                                       ...with bias $7FFD

Chk_X:
   movew               -12(%a0),%d0
   movew               %d0,SignX(%a6)
   movew               SignY(%a6),%d1
   eorl                %d0,%d1
   andil               #0x00008000,%d1
   movew               %d1,SignQ(%a6)	| ...sign(Q) obtained
   andil               #0x00007FFF,%d0
   movel               -8(%a0),%d1
   movel               -4(%a0),%d2       | ...(D0,D1,D2) is |X|
   tstl                %d0
   bnes                X_Normal
   movel               #0x00003FFE,%d0
   tstl                %d1
   bnes                HiX_not0

HiX_0:
   movel               %d2,%d1
   clrl                %d2
   subil               #32,%d0
   clrl                %d6
   bfffo                %d1{#0:#32},%d6
   lsll                %d6,%d1
   subl                %d6,%d0           | ...(D0,D1,D2) is normalized
|                                       ...with bias $7FFD
   bras                Init

HiX_not0:
   clrl                %d6
   bfffo                %d1{#0:#32},%d6
   subl                %d6,%d0
   lsll                %d6,%d1
   movel               %d2,%d7           | ...a copy of D2
   lsll                %d6,%d2
   negl                %d6
   addil               #32,%d6
   lsrl                %d6,%d7
   orl                 %d7,%d1           | ...(D0,D1,D2) normalized
|                                       ...with bias $7FFD
   bras                Init

X_Normal:
   addil               #0x00003FFE,%d0   | ...(D0,D1,D2) normalized
|                                       ...with bias $7FFD

Init:
|
   movel               %d3,L_SCR1(%a6)   | ...save biased expo(Y)
   movel		%d0,L_SCR2(%a6)	|save d0
   subl                %d3,%d0           | ...L := expo(X)-expo(Y)
|   Move.L               D0,L            ...D0 is j
   clrl                %d6              | ...D6 := carry <- 0
   clrl                %d3              | ...D3 is Q
   moveal              #0,%a1           | ...A1 is k; j+k=L, Q=0

|..(Carry,D1,D2) is R
   tstl                %d0
   bges                Mod_Loop

|..expo(X) < expo(Y). Thus X = mod(X,Y)
|
   movel		L_SCR2(%a6),%d0	|restore d0
   bra                Get_Mod

|..At this point  R = 2^(-L)X; Q = 0; k = 0; and  k+j = L


Mod_Loop:
   tstl                %d6              | ...test carry bit
   bgts                R_GT_Y

|..At this point carry = 0, R = (D1,D2), Y = (D4,D5)
   cmpl                %d4,%d1           | ...compare hi(R) and hi(Y)
   bnes                R_NE_Y
   cmpl                %d5,%d2           | ...compare lo(R) and lo(Y)
   bnes                R_NE_Y

|..At this point, R = Y
   bra                Rem_is_0

R_NE_Y:
|..use the borrow of the previous compare
   bcss                R_LT_Y          | ...borrow is set iff R < Y

R_GT_Y:
|..If Carry is set, then Y < (Carry,D1,D2) < 2Y. Otherwise, Carry = 0
|..and Y < (D1,D2) < 2Y. Either way, perform R - Y
   subl                %d5,%d2           | ...lo(R) - lo(Y)
   subxl               %d4,%d1           | ...hi(R) - hi(Y)
   clrl                %d6              | ...clear carry
   addql               #1,%d3           | ...Q := Q + 1

R_LT_Y:
|..At this point, Carry=0, R < Y. R = 2^(k-L)X - QY; k+j = L; j >= 0.
   tstl                %d0              | ...see if j = 0.
   beqs                PostLoop

   addl                %d3,%d3           | ...Q := 2Q
   addl                %d2,%d2           | ...lo(R) = 2lo(R)
   roxll               #1,%d1           | ...hi(R) = 2hi(R) + carry
   scs                  %d6              | ...set Carry if 2(R) overflows
   addql               #1,%a1           | ...k := k+1
   subql               #1,%d0           | ...j := j - 1
|..At this point, R=(Carry,D1,D2) = 2^(k-L)X - QY, j+k=L, j >= 0, R < 2Y.

   bras                Mod_Loop

PostLoop:
|..k = L, j = 0, Carry = 0, R = (D1,D2) = X - QY, R < Y.

|..normalize R.
   movel               L_SCR1(%a6),%d0           | ...new biased expo of R
   tstl                %d1
   bnes                HiR_not0

HiR_0:
   movel               %d2,%d1
   clrl                %d2
   subil               #32,%d0
   clrl                %d6
   bfffo                %d1{#0:#32},%d6
   lsll                %d6,%d1
   subl                %d6,%d0           | ...(D0,D1,D2) is normalized
|                                       ...with bias $7FFD
   bras                Get_Mod

HiR_not0:
   clrl                %d6
   bfffo                %d1{#0:#32},%d6
   bmis                Get_Mod         | ...already normalized
   subl                %d6,%d0
   lsll                %d6,%d1
   movel               %d2,%d7           | ...a copy of D2
   lsll                %d6,%d2
   negl                %d6
   addil               #32,%d6
   lsrl                %d6,%d7
   orl                 %d7,%d1           | ...(D0,D1,D2) normalized

|
Get_Mod:
   cmpil		#0x000041FE,%d0
   bges		No_Scale
Do_Scale:
   movew		%d0,R(%a6)
   clrw		R+2(%a6)
   movel		%d1,R_Hi(%a6)
   movel		%d2,R_Lo(%a6)
   movel		L_SCR1(%a6),%d6
   movew		%d6,Y(%a6)
   clrw		Y+2(%a6)
   movel		%d4,Y_Hi(%a6)
   movel		%d5,Y_Lo(%a6)
   fmovex		R(%a6),%fp0		| ...no exception
   movel		#1,Sc_Flag(%a6)
   bras		ModOrRem
No_Scale:
   movel		%d1,R_Hi(%a6)
   movel		%d2,R_Lo(%a6)
   subil		#0x3FFE,%d0
   movew		%d0,R(%a6)
   clrw		R+2(%a6)
   movel		L_SCR1(%a6),%d6
   subil		#0x3FFE,%d6
   movel		%d6,L_SCR1(%a6)
   fmovex		R(%a6),%fp0
   movew		%d6,Y(%a6)
   movel		%d4,Y_Hi(%a6)
   movel		%d5,Y_Lo(%a6)
   movel		#0,Sc_Flag(%a6)

|


ModOrRem:
   movel               Mod_Flag(%a6),%d6
   beqs                Fix_Sign

   movel               L_SCR1(%a6),%d6           | ...new biased expo(Y)
   subql               #1,%d6           | ...biased expo(Y/2)
   cmpl                %d6,%d0
   blts                Fix_Sign
   bgts                Last_Sub

   cmpl                %d4,%d1
   bnes                Not_EQ
   cmpl                %d5,%d2
   bnes                Not_EQ
   bra                Tie_Case

Not_EQ:
   bcss                Fix_Sign

Last_Sub:
|
   fsubx		Y(%a6),%fp0		| ...no exceptions
   addql               #1,%d3           | ...Q := Q + 1

|

Fix_Sign:
|..Get sign of X
   movew               SignX(%a6),%d6
   bges		Get_Q
   fnegx		%fp0

|..Get Q
|
Get_Q:
   clrl		%d6
   movew               SignQ(%a6),%d6        | ...D6 is sign(Q)
   movel               #8,%d7
   lsrl                %d7,%d6
   andil               #0x0000007F,%d3   | ...7 bits of Q
   orl                 %d6,%d3           | ...sign and bits of Q
   swap                 %d3
   fmovel              %fpsr,%d6
   andil               #0xFF00FFFF,%d6
   orl                 %d3,%d6
   fmovel              %d6,%fpsr         | ...put Q in fpsr

|
Restore:
   moveml              (%a7)+,%d2-%d7
   fmovel              USER_FPCR(%a6),%fpcr
   movel               Sc_Flag(%a6),%d0
   beqs                Finish
   fmulx		Scale(%pc),%fp0	| ...may cause underflow
   bra			t_avoid_unsupp	|check for denorm as a
|					;result of the scaling

Finish:
	fmovex		%fp0,%fp0		|capture exceptions & round
	rts

Rem_is_0:
|..R = 2^(-j)X - Q Y = Y, thus R = 0 and quotient = 2^j (Q+1)
   addql               #1,%d3
   cmpil               #8,%d0           | ...D0 is j
   bges                Q_Big

   lsll                %d0,%d3
   bras                Set_R_0

Q_Big:
   clrl                %d3

Set_R_0:
   fmoves		#0x00000000,%fp0
   movel		#0,Sc_Flag(%a6)
   bra                Fix_Sign

Tie_Case:
|..Check parity of Q
   movel               %d3,%d6
   andil               #0x00000001,%d6
   tstl                %d6
   beq                Fix_Sign	| ...Q is even

|..Q is odd, Q := Q + 1, signX := -signX
   addql               #1,%d3
   movew               SignX(%a6),%d6
   eoril               #0x00008000,%d6
   movew               %d6,SignX(%a6)
   bra                Fix_Sign

   |end
