#ifndef __KTAP_BYTECODE_H__
#define __KTAP_BYTECODE_H__


/* opcode is copied from lua initially */
 
typedef enum {
/*----------------------------------------------------------------------
 * name            args    description
 * ------------------------------------------------------------------------*/
OP_MOVE,/*      A B     R(A) := R(B)                                    */
OP_LOADK,/*     A Bx    R(A) := Kst(Bx)                                 */
OP_LOADKX,/*    A       R(A) := Kst(extra arg)                          */
OP_LOADBOOL,/*  A B C   R(A) := (Bool)B; if (C) pc++                    */
OP_LOADNIL,/*   A B     R(A), R(A+1), ..., R(A+B) := nil                */
OP_GETUPVAL,/*  A B     R(A) := UpValue[B]                              */

OP_GETTABUP,/*  A B C   R(A) := UpValue[B][RK(C)]                       */
OP_GETTABLE,/*  A B C   R(A) := R(B)[RK(C)]                             */

OP_SETTABUP,/*  A B C   UpValue[A][RK(B)] := RK(C)                      */
OP_SETTABUP_INCR,/*  A B C   UpValue[A][RK(B)] += RK(C)                 */
OP_SETUPVAL,/*  A B     UpValue[B] := R(A)                              */
OP_SETTABLE,/*  A B C   R(A)[RK(B)] := RK(C)                            */
OP_SETTABLE_INCR,/*  A B C   R(A)[RK(B)] += RK(C)                       */

OP_NEWTABLE,/*  A B C   R(A) := {} (size = B,C)                         */

OP_SELF,/*      A B C   R(A+1) := R(B); R(A) := R(B)[RK(C)]             */

OP_ADD,/*       A B C   R(A) := RK(B) + RK(C)                           */
OP_SUB,/*       A B C   R(A) := RK(B) - RK(C)                           */
OP_MUL,/*       A B C   R(A) := RK(B) * RK(C)                           */
OP_DIV,/*       A B C   R(A) := RK(B) / RK(C)                           */
OP_MOD,/*       A B C   R(A) := RK(B) % RK(C)                           */
OP_POW,/*       A B C   R(A) := RK(B) ^ RK(C)                           */
OP_UNM,/*       A B     R(A) := -R(B)                                   */
OP_NOT,/*       A B     R(A) := not R(B)                                */
OP_LEN,/*       A B     R(A) := length of R(B)                          */

OP_CONCAT,/*    A B C   R(A) := R(B).. ... ..R(C)                       */

OP_JMP,/*       A sBx   pc+=sBx; if (A) close all upvalues >= R(A) + 1  */
OP_EQ,/*        A B C   if ((RK(B) == RK(C)) != A) then pc++            */
OP_LT,/*        A B C   if ((RK(B) <  RK(C)) != A) then pc++            */
OP_LE,/*        A B C   if ((RK(B) <= RK(C)) != A) then pc++            */

OP_TEST,/*      A C     if not (R(A) <=> C) then pc++                   */
OP_TESTSET,/*   A B C   if (R(B) <=> C) then R(A) := R(B) else pc++     */

OP_CALL,/*      A B C   R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) */
OP_TAILCALL,/*  A B C   return R(A)(R(A+1), ... ,R(A+B-1))              */
OP_RETURN,/*    A B     return R(A), ... ,R(A+B-2)      (see note)      */

OP_FORLOOP,/*   A sBx   R(A)+=R(A+2);
                        if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) }*/
OP_FORPREP,/*   A sBx   R(A)-=R(A+2); pc+=sBx                           */

OP_TFORCALL,/*  A C     R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2));  */
OP_TFORLOOP,/*  A sBx   if R(A+1) != nil then { R(A)=R(A+1); pc += sBx }*/

OP_SETLIST,/*   A B C   R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B        */

OP_CLOSURE,/*   A Bx    R(A) := closure(KPROTO[Bx])                     */

OP_VARARG,/*    A B     R(A), R(A+1), ..., R(A+B-2) = vararg            */

OP_EXTRAARG,/*   Ax      extra (larger) argument for previous opcode     */

OP_EVENT,/*  A B C   R(A) := R(B)[C]                             */

OP_EVENTNAME, /* A	R(A) = event_name() */

OP_EVENTARG,/* A B	R(A) := event_arg(B)*/

OP_LOAD_GLOBAL,/*  A B C   R(A) := R(B)[C]                             */

OP_EXIT,

} OpCode;


#define NUM_OPCODES     ((int)OP_LOAD_GLOBAL + 1)


enum OpMode {iABC, iABx, iAsBx, iAx};  /* basic instruction format */


/*
 * ** size and position of opcode arguments.
 * */
#define SIZE_C          9
#define SIZE_B          9
#define SIZE_Bx         (SIZE_C + SIZE_B)
#define SIZE_A          8
#define SIZE_Ax         (SIZE_C + SIZE_B + SIZE_A)

#define SIZE_OP         6

#define POS_OP          0
#define POS_A           (POS_OP + SIZE_OP)
#define POS_C           (POS_A + SIZE_A)
#define POS_B           (POS_C + SIZE_C)
#define POS_Bx          POS_C
#define POS_Ax          POS_A



/*
 * ** limits for opcode arguments.
 * ** we use (signed) int to manipulate most arguments,
 * ** so they must fit in LUAI_BITSINT-1 bits (-1 for sign)
 * */
#define MAXARG_Bx        ((1<<SIZE_Bx)-1)
#define MAXARG_sBx        (MAXARG_Bx>>1)         /* `sBx' is signed */

#define MAXARG_Ax       ((1<<SIZE_Ax)-1)

#define MAXARG_A        ((1<<SIZE_A)-1)
#define MAXARG_B        ((1<<SIZE_B)-1)
#define MAXARG_C        ((1<<SIZE_C)-1)


/* creates a mask with `n' 1 bits at position `p' */
#define MASK1(n,p)      ((~((~(ktap_instruction)0)<<(n)))<<(p))

/* creates a mask with `n' 0 bits at position `p' */
#define MASK0(n,p)      (~MASK1(n,p))

/*
 * ** the following macros help to manipulate instructions
 * */

#define GET_OPCODE(i)   ((OpCode)((i)>>POS_OP) & MASK1(SIZE_OP,0))
#define SET_OPCODE(i,o) ((i) = (((i)&MASK0(SIZE_OP,POS_OP)) | \
                ((((ktap_instruction)o)<<POS_OP)&MASK1(SIZE_OP,POS_OP))))

#define getarg(i,pos,size)      ((int)((i)>>pos) & MASK1(size,0))
#define setarg(i,v,pos,size)    ((i) = (((i)&MASK0(size,pos)) | \
                ((((ktap_instruction)v)<<pos)&MASK1(size,pos))))

#define GETARG_A(i)     getarg(i, POS_A, SIZE_A)
#define SETARG_A(i,v)   setarg(i, v, POS_A, SIZE_A)

#define GETARG_A(i)     getarg(i, POS_A, SIZE_A)
#define SETARG_A(i,v)   setarg(i, v, POS_A, SIZE_A)

#define GETARG_B(i)     getarg(i, POS_B, SIZE_B)
#define SETARG_B(i,v)   setarg(i, v, POS_B, SIZE_B)

#define GETARG_C(i)     getarg(i, POS_C, SIZE_C)
#define SETARG_C(i,v)   setarg(i, v, POS_C, SIZE_C)

#define GETARG_Bx(i)    getarg(i, POS_Bx, SIZE_Bx)
#define SETARG_Bx(i,v)  setarg(i, v, POS_Bx, SIZE_Bx)

#define GETARG_Ax(i)    getarg(i, POS_Ax, SIZE_Ax)
#define SETARG_Ax(i,v)  setarg(i, v, POS_Ax, SIZE_Ax)

#define GETARG_sBx(i)   (GETARG_Bx(i)-MAXARG_sBx)
#define SETARG_sBx(i,b) SETARG_Bx((i), (unsigned int)(b)+MAXARG_sBx)

#define CREATE_ABC(o,a,b,c)     (((ktap_instruction)(o))<<POS_OP) \
                        | (((ktap_instruction)(a))<<POS_A) \
                        | (((ktap_instruction)(b))<<POS_B) \
                        | (((ktap_instruction)(c))<<POS_C)

#define CREATE_ABx(o,a,bc)      (((ktap_instruction)(o))<<POS_OP) \
                        | (((ktap_instruction)(a))<<POS_A) \
                        | (((ktap_instruction)(bc))<<POS_Bx)

#define CREATE_Ax(o,a)          (((ktap_instruction)(o))<<POS_OP) \
                        | (((ktap_instruction)(a))<<POS_Ax)



/*
 * ** Macros to operate RK indices
 * */

/* this bit 1 means constant (0 means register) */
#define BITRK           (1 << (SIZE_B - 1))

/* test whether value is a constant */
#define ISK(x)          ((x) & BITRK)

/* gets the index of the constant */
#define INDEXK(r)       ((int)(r) & ~BITRK)

#define MAXINDEXRK      (BITRK - 1)

/* code a constant index as a RK value */
#define RKASK(x)        ((x) | BITRK)


/*
 * ** invalid register that fits in 8 bits
 * */
#define NO_REG          MAXARG_A


/*
 * ** R(x) - register
 * ** Kst(x) - constant (in constant table)
 * ** RK(x) == if ISK(x) then Kst(INDEXK(x)) else R(x)
 * */



/*
 * ** masks for instruction properties. The format is:
 * ** bits 0-1: op mode
 * ** bits 2-3: C arg mode
 * ** bits 4-5: B arg mode
 * ** bit 6: instruction set register A
 * ** bit 7: operator is a test (next instruction must be a jump)
 * */

enum OpArgMask {
  OpArgN,  /* argument is not used */
  OpArgU,  /* argument is used */
  OpArgR,  /* argument is a register or a jump offset */
  OpArgK   /* argument is a constant or register/constant */
};

extern const u8 ktap_opmodes[NUM_OPCODES];

#define getOpMode(m)    ((enum OpMode)ktap_opmodes[m] & 3)
#define getBMode(m)     ((enum OpArgMask)(ktap_opmodes[m] >> 4) & 3)
#define getCMode(m)     ((enum OpArgMask)(ktap_opmodes[m] >> 2) & 3)
#define testAMode(m)    (ktap_opmodes[m] & (1 << 6))
#define testTMode(m)    (ktap_opmodes[m] & (1 << 7))


/* number of list items to accumulate before a SETLIST instruction */
#define LFIELDS_PER_FLUSH       50

extern const char *const ktap_opnames[NUM_OPCODES + 1];

#endif /* __KTAP_BYTECODE_H__ */
