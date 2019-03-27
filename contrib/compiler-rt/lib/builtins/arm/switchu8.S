//===-- switch.S - Implement switch* --------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// When compiling switch statements in thumb mode, the compiler
// can use these __switch* helper functions  The compiler emits a blx to
// the __switch* function followed by a table of displacements for each
// case statement.  On entry, R0 is the index into the table. The __switch*
// function uses the return address in lr to find the start of the table.
// The first entry in the table is the count of the entries in the table.
// It then uses R0 to index into the table and get the displacement of the
// address to jump to.  If R0 is greater than the size of the table, it jumps
// to the last entry in the table. Each displacement in the table is actually
// the distance from lr to the label, thus making the tables PIC.


	.text
	.syntax unified

//
// The table contains unsigned byte sized elements which are 1/2 the distance
// from lr to the target label.
//
	.p2align 2
DEFINE_COMPILERRT_PRIVATE_FUNCTION(__switchu8)
	ldrb    ip, [lr, #-1]           // get first byte in table
	cmp     r0, ip                  // compare with index
	ite lo
	ldrblo  r0, [lr, r0]            // get indexed byte out of table
	ldrbhs  r0, [lr, ip]            // if out of range, use last entry in table
	add     ip, lr, r0, lsl #1      // compute label = lr + element*2
	bx      ip                      // jump to computed label
END_COMPILERRT_FUNCTION(__switchu8)

NO_EXEC_STACK_DIRECTIVE

