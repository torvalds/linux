
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "cpu_test.h"

.text

//r0,array0
ENTRY(test_cpus_l1_0)

	stmfd	sp!, { r1 - r12, lr }
1: 	mov r0,r0
	//b 1b
	mov r4,r0

	test_cpus_l1_loop_200_k
	test_cpus_l1_loop_200_k
	test_cpus_l1_loop_200_k
	test_cpus_l1_loop_200_k
	test_cpus_l1_loop_200_k

	test_cpus_l1_loop_200_k
	test_cpus_l1_loop_200_k
	test_cpus_l1_loop_200_k
	test_cpus_l1_loop_200_k
	test_cpus_l1_loop_200_k

	//test_cpus_l1_loop_200_k
	//test_cpus_l1_loop_200_k
	//test_cpus_l1_loop_200_k


1: mov r1,r1
	//b 1b
	ldmfd	sp!, { r1 - r12, pc }
l1_test_error:
	mov r1,r1
	mov r1,r1
	mov r1,r1
	1: mov r1,r1
	b 1b
ENDPROC(test_cpus_l1_0)


//r0 adr base,r1 end adr,r2 tst date
ENTRY(test_cpus_l2)

	stmfd	sp!, { r3 - r12, lr }
1: 	mov r0,r0
	//b 1b
	
	mov r4,r0
	sub r5,r1,#4
	
l2_write:	
	str r2,[r4],#4
		
	cmp r4,r5
	bls	l2_write

	mov r4,r0
l2_check:		
	ldr  	r6,[r4],#4
	cmp r6,r2
	bne l2_test_error
	mov r6,#0
	cmp r4,r5
	bhi l2_tst_end
	
	ldr r7,[r4],#4
	cmp r7,r2
	bne l2_test_error
	mov r7,#0
	
	cmp r4,r5
	bls l2_check
	
l2_tst_end:	
3: mov r0,#0
	//b 3b
	ldmfd	sp!, { r3 - r12, pc }
l2_test_error:
1: mov r0,#1
	//b 1b
	ldmfd	sp!, { r3 - r12, pc }
ENDPROC(test_cpus_l2)

//r0 adr base,r1 end adr,r2 tst date
ENTRY(test_cpus_mem_set)

	stmfd	sp!, { r3 - r12, lr }
1: 	mov r0,r0
	//b 1b
	
	mov r4,r0
	sub r5,r1,#4
	
mem_set:	
	str r2,[r4],#4
		
	cmp r4,r5
	bls	mem_set
	
1: mov r0,#1
	//b 1b
	ldmfd	sp!, { r3 - r12, pc }
ENDPROC(test_cpus_mem_set)



//r0 adr base,r1 end adr,r2 tst date
ENTRY(test_cpus_mem_check)

	stmfd	sp!, { r3 - r12, lr }
1:	mov r0,r0
	//b 1b
	mov r4,r0
	sub r5,r1,#4
	
mem_check:		
	ldr 	r6,[r4],#4
	cmp r6,r2
	bne mem_check_error
	mov r6,#0
	cmp r4,r5
	bhi mem_check_end
	
	ldr r7,[r4],#4
	cmp r7,r2
	bne mem_check_error
	mov r7,#0
	
	cmp r4,r5
	bls mem_check
	
mem_check_end: 
3: mov r0,#0
	//b 3b
	ldmfd	sp!, { r3 - r12, pc }
mem_check_error:
1: mov r0,#1
	//b 1b
	ldmfd	sp!, { r3 - r12, pc }

ENDPROC(test_cpus_mem_check)
/***************while for dbg***************/
ENTRY(rk30_cpu_while_tst)
stmfd	sp!, { r3 - r12, lr }

1: mov r3,r3
   b 1b

ldmfd	sp!, { r3 - r12, pc }

ENDPROC(rk30_cpu_while_tst)

