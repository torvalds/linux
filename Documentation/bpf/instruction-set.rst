
====================
eBPF Instruction Set
====================

Registers and calling convention
================================

eBPF has 10 general purpose registers and a read-only frame pointer register,
all of which are 64-bits wide.

The eBPF calling convention is defined as:

 * R0: return value from function calls, and exit value for eBPF programs
 * R1 - R5: arguments for function calls
 * R6 - R9: callee saved registers that function calls will preserve
 * R10: read-only frame pointer to access stack

R0 - R5 are scratch registers and eBPF programs needs to spill/fill them if
necessary across calls.

Instruction encoding
====================

eBPF uses 64-bit instructions with the following encoding:

 =============  =======  ===============  ====================  ============
 32 bits (MSB)  16 bits  4 bits           4 bits                8 bits (LSB)
 =============  =======  ===============  ====================  ============
 immediate      offset   source register  destination register  opcode
 =============  =======  ===============  ====================  ============

Note that most instructions do not use all of the fields.
Unused fields shall be cleared to zero.

Instruction classes
-------------------

The three LSB bits of the 'opcode' field store the instruction class:

  =========  =====  ===============================
  class      value  description
  =========  =====  ===============================
  BPF_LD     0x00   non-standard load operations
  BPF_LDX    0x01   load into register operations
  BPF_ST     0x02   store from immediate operations
  BPF_STX    0x03   store from register operations
  BPF_ALU    0x04   32-bit arithmetic operations
  BPF_JMP    0x05   64-bit jump operations
  BPF_JMP32  0x06   32-bit jump operations
  BPF_ALU64  0x07   64-bit arithmetic operations
  =========  =====  ===============================

Arithmetic and jump instructions
================================

For arithmetic and jump instructions (BPF_ALU, BPF_ALU64, BPF_JMP and
BPF_JMP32), the 8-bit 'opcode' field is divided into three parts:

  ==============  ======  =================
  4 bits (MSB)    1 bit   3 bits (LSB)
  ==============  ======  =================
  operation code  source  instruction class
  ==============  ======  =================

The 4th bit encodes the source operand:

  ======  =====  ========================================
  source  value  description
  ======  =====  ========================================
  BPF_K   0x00   use 32-bit immediate as source operand
  BPF_X   0x08   use 'src_reg' register as source operand
  ======  =====  ========================================

The four MSB bits store the operation code.


Arithmetic instructions
-----------------------

BPF_ALU uses 32-bit wide operands while BPF_ALU64 uses 64-bit wide operands for
otherwise identical operations.
The code field encodes the operation as below:

  ========  =====  ==========================
  code      value  description
  ========  =====  ==========================
  BPF_ADD   0x00   dst += src
  BPF_SUB   0x10   dst -= src
  BPF_MUL   0x20   dst \*= src
  BPF_DIV   0x30   dst /= src
  BPF_OR    0x40   dst \|= src
  BPF_AND   0x50   dst &= src
  BPF_LSH   0x60   dst <<= src
  BPF_RSH   0x70   dst >>= src
  BPF_NEG   0x80   dst = ~src
  BPF_MOD   0x90   dst %= src
  BPF_XOR   0xa0   dst ^= src
  BPF_MOV   0xb0   dst = src
  BPF_ARSH  0xc0   sign extending shift right
  BPF_END   0xd0   endianness conversion
  ========  =====  ==========================

BPF_ADD | BPF_X | BPF_ALU means::

  dst_reg = (u32) dst_reg + (u32) src_reg;

BPF_ADD | BPF_X | BPF_ALU64 means::

  dst_reg = dst_reg + src_reg

BPF_XOR | BPF_K | BPF_ALU means::

  src_reg = (u32) src_reg ^ (u32) imm32

BPF_XOR | BPF_K | BPF_ALU64 means::

  src_reg = src_reg ^ imm32


Jump instructions
-----------------

BPF_JMP32 uses 32-bit wide operands while BPF_JMP uses 64-bit wide operands for
otherwise identical operations.
The code field encodes the operation as below:

  ========  =====  =========================  ============
  code      value  description                notes
  ========  =====  =========================  ============
  BPF_JA    0x00   PC += off                  BPF_JMP only
  BPF_JEQ   0x10   PC += off if dst == src
  BPF_JGT   0x20   PC += off if dst > src     unsigned
  BPF_JGE   0x30   PC += off if dst >= src    unsigned
  BPF_JSET  0x40   PC += off if dst & src
  BPF_JNE   0x50   PC += off if dst != src
  BPF_JSGT  0x60   PC += off if dst > src     signed
  BPF_JSGE  0x70   PC += off if dst >= src    signed
  BPF_CALL  0x80   function call
  BPF_EXIT  0x90   function / program return  BPF_JMP only
  BPF_JLT   0xa0   PC += off if dst < src     unsigned
  BPF_JLE   0xb0   PC += off if dst <= src    unsigned
  BPF_JSLT  0xc0   PC += off if dst < src     signed
  BPF_JSLE  0xd0   PC += off if dst <= src    signed
  ========  =====  =========================  ============

The eBPF program needs to store the return value into register R0 before doing a
BPF_EXIT.


Load and store instructions
===========================

For load and store instructions (BPF_LD, BPF_LDX, BPF_ST and BPF_STX), the
8-bit 'opcode' field is divided as:

  ============  ======  =================
  3 bits (MSB)  2 bits  3 bits (LSB)
  ============  ======  =================
  mode          size    instruction class
  ============  ======  =================

The size modifier is one of:

  =============  =====  =====================
  size modifier  value  description
  =============  =====  =====================
  BPF_W          0x00   word        (4 bytes)
  BPF_H          0x08   half word   (2 bytes)
  BPF_B          0x10   byte
  BPF_DW         0x18   double word (8 bytes)
  =============  =====  =====================

The mode modifier is one of:

  =============  =====  ====================================
  mode modifier  value  description
  =============  =====  ====================================
  BPF_IMM        0x00   used for 64-bit mov
  BPF_ABS        0x20   legacy BPF packet access
  BPF_IND        0x40   legacy BPF packet access
  BPF_MEM        0x60   all normal load and store operations
  BPF_ATOMIC     0xc0   atomic operations
  =============  =====  ====================================

BPF_MEM | <size> | BPF_STX means::

  *(size *) (dst_reg + off) = src_reg

BPF_MEM | <size> | BPF_ST means::

  *(size *) (dst_reg + off) = imm32

BPF_MEM | <size> | BPF_LDX means::

  dst_reg = *(size *) (src_reg + off)

Where size is one of: BPF_B or BPF_H or BPF_W or BPF_DW.

Atomic operations
-----------------

eBPF includes atomic operations, which use the immediate field for extra
encoding::

   .imm = BPF_ADD, .code = BPF_ATOMIC | BPF_W  | BPF_STX: lock xadd *(u32 *)(dst_reg + off16) += src_reg
   .imm = BPF_ADD, .code = BPF_ATOMIC | BPF_DW | BPF_STX: lock xadd *(u64 *)(dst_reg + off16) += src_reg

The basic atomic operations supported are::

    BPF_ADD
    BPF_AND
    BPF_OR
    BPF_XOR

Each having equivalent semantics with the ``BPF_ADD`` example, that is: the
memory location addresed by ``dst_reg + off`` is atomically modified, with
``src_reg`` as the other operand. If the ``BPF_FETCH`` flag is set in the
immediate, then these operations also overwrite ``src_reg`` with the
value that was in memory before it was modified.

The more special operations are::

    BPF_XCHG

This atomically exchanges ``src_reg`` with the value addressed by ``dst_reg +
off``. ::

    BPF_CMPXCHG

This atomically compares the value addressed by ``dst_reg + off`` with
``R0``. If they match it is replaced with ``src_reg``. In either case, the
value that was there before is zero-extended and loaded back to ``R0``.

Note that 1 and 2 byte atomic operations are not supported.

Clang can generate atomic instructions by default when ``-mcpu=v3`` is
enabled. If a lower version for ``-mcpu`` is set, the only atomic instruction
Clang can generate is ``BPF_ADD`` *without* ``BPF_FETCH``. If you need to enable
the atomics features, while keeping a lower ``-mcpu`` version, you can use
``-Xclang -target-feature -Xclang +alu32``.

You may encounter ``BPF_XADD`` - this is a legacy name for ``BPF_ATOMIC``,
referring to the exclusive-add operation encoded when the immediate field is
zero.

16-byte instructions
--------------------

eBPF has one 16-byte instruction: ``BPF_LD | BPF_DW | BPF_IMM`` which consists
of two consecutive ``struct bpf_insn`` 8-byte blocks and interpreted as single
instruction that loads 64-bit immediate value into a dst_reg.

Packet access instructions
--------------------------

eBPF has two non-generic instructions: (BPF_ABS | <size> | BPF_LD) and
(BPF_IND | <size> | BPF_LD) which are used to access packet data.

They had to be carried over from classic BPF to have strong performance of
socket filters running in eBPF interpreter. These instructions can only
be used when interpreter context is a pointer to ``struct sk_buff`` and
have seven implicit operands. Register R6 is an implicit input that must
contain pointer to sk_buff. Register R0 is an implicit output which contains
the data fetched from the packet. Registers R1-R5 are scratch registers
and must not be used to store the data across BPF_ABS | BPF_LD or
BPF_IND | BPF_LD instructions.

These instructions have implicit program exit condition as well. When
eBPF program is trying to access the data beyond the packet boundary,
the interpreter will abort the execution of the program. JIT compilers
therefore must preserve this property. src_reg and imm32 fields are
explicit inputs to these instructions.

For example, BPF_IND | BPF_W | BPF_LD means::

  R0 = ntohl(*(u32 *) (((struct sk_buff *) R6)->data + src_reg + imm32))

and R1 - R5 are clobbered.
