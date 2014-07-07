
#include <linux/linkage.h>
#include <asm/assembler.h>
//#include <asm/memory.h>
//#include <mach/io.h>
#include "cpu_test.h"

.text

//r0,array0
ENTRY(test_cpus_l1_1)
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


1: mov r1,r1
	//b 1b
	ldmfd	sp!, { r1- r12, pc }
l1_test_error:
1: mov r1,r1
	b 1b
ENDPROC(test_cpus_l1_1)







