
#include <linux/linkage.h>
#include <asm/assembler.h>
//#include <asm/memory.h>
//#include <mach/io.h>

.text


	//_off 12 -20
	.macro	test_cpus_pc_jump , _base,_reg, _off
	
	ldr \_reg,[pc]
	str \_reg,[\_base]
	mov \_reg,pc
	str \_reg,[\_base,#4]
	add \_reg,\_off	//pc offset
	str \_reg,[\_base,#8]// offset start
	ldr   pc, [\_base,#8]
	ldr \_reg,[pc]
	ldr \_reg,[pc]
	.endm


	.macro	cpus_tst_get_opcode, _reg0
	lsl \_reg0,\_reg0,#4
	lsr \_reg0,\_reg0,#24
	.endm


	.macro	cpus_tst_code_lsl , _reg0
	ldr \_reg0,[pc]
	mov \_reg0,\_reg0
	lsl \_reg0,\_reg0,#4
	lsr \_reg0,\_reg0,#24
	cmp \_reg0,#0x1a
	bne l1_test_error
	.endm

	.macro	cpus_tst_code_lsr , _reg0
	ldr \_reg0,[pc]
	lsl \_reg0,\_reg0,#4
	lsr \_reg0,\_reg0,#24
	cmp \_reg0,#0x1a
	bne l1_test_error
	.endm


	.macro	cpus_tst_code_cmp, _reg0
	ldr \_reg0,[pc,#4]
	lsl \_reg0,\_reg0,#4
	lsr \_reg0,\_reg0,#24
	cmp \_reg0,#0x35
	bne l1_test_error
	.endm



	.macro	cpus_tst_code_sub, _reg0,_reg1
	mov \_reg0,pc
	sub \_reg0,#4
	ldr \_reg1,[\_reg0]
	lsl \_reg1,\_reg1,#4
	lsr \_reg1,\_reg1,#24
	cmp \_reg1,#0x24
	bne l1_test_error
	.endm


	.macro	cpus_tst_code_ldr, _reg0,_reg1
	mov \_reg0,pc
	sub \_reg0,#0
	ldr \_reg1,[\_reg0]
	lsl \_reg1,\_reg1,#4
	lsr \_reg1,\_reg1,#24
	cmp \_reg1,#0x59
	bne l1_test_error
	.endm

	.macro	cpus_tst_code_bne, _reg0,_reg1
	ldr \_reg0,[pc,#12]

	lsr \_reg1,\_reg0,#28
	lsl \_reg0,\_reg0,#4
	lsr \_reg0,\_reg0,#24
	
	cmp \_reg1,#0x1 //
	bne l1_test_error
	cmp \_reg0,#0xa0
	bne l1_test_error
	
	.endm



.macro	test_cpus_l1_code_base
	//r4 base
	mov r5,#16
	test_cpus_pc_jump r4,r3,r5
	
	//add r4,#8
	mov r6,r4
	mov r8,#20
	test_cpus_pc_jump r6,r7,r8 

	//add r4,#8

	cpus_tst_code_lsl r5
	
	cpus_tst_code_lsr r7
	
	cpus_tst_code_cmp r9


	cpus_tst_code_sub r11,r10,
	
	cpus_tst_code_ldr r12,r8
	
	cpus_tst_code_bne r6,r7

.endm


.macro	test_cpus_l1_code_base_

	
	mov r4,r4
	mov r6,r6
	mov r7,r7
	mov r8,r8
	mov r4,r4
	mov r6,r6
	mov r7,r7
	mov r8,r8
	mov r4,r4
	mov r6,r6

	mov r4,r4
	mov r6,r6
	mov r7,r7
	mov r8,r8
	mov r4,r4
	mov r6,r6
	mov r7,r7
	mov r8,r8
	mov r4,r4
	mov r6,r6



	mov r7,r7
	mov r8,r8
	mov r4,r4
	mov r6,r6
	mov r7,r7
	
	
	
	

		
.endm


.macro	test_cpus_l1_loop_100

.endm

.macro	test_cpus_l1_loop_500
#if 1
	test_cpus_l1_code_base  
	test_cpus_l1_code_base  
#else

	test_cpus_l1_code_base_
	test_cpus_l1_code_base_

	test_cpus_l1_code_base_
	test_cpus_l1_code_base_
	test_cpus_l1_code_base_
	
	test_cpus_l1_code_base_

#endif
.endm


.macro	test_cpus_l1_loop_1_k
	test_cpus_l1_loop_500  
	test_cpus_l1_loop_500  
.endm


.macro	test_cpus_l1_loop_4_k
	test_cpus_l1_loop_1_k  
	test_cpus_l1_loop_1_k  
	test_cpus_l1_loop_1_k	
	test_cpus_l1_loop_1_k	
	test_cpus_l1_loop_500	
.endm



.macro	test_cpus_l1_loop_10_k
	test_cpus_l1_loop_4_k  
	test_cpus_l1_loop_4_k  
	test_cpus_l1_loop_4_k
	test_cpus_l1_loop_4_k
.endm


.macro	test_cpus_l1_loop_50_k
	test_cpus_l1_loop_10_k  
	test_cpus_l1_loop_10_k  
	test_cpus_l1_loop_10_k
	test_cpus_l1_loop_10_k
	test_cpus_l1_loop_10_k
.endm

.macro	test_cpus_l1_loop_200_k
	test_cpus_l1_loop_50_k  
	test_cpus_l1_loop_50_k  
	test_cpus_l1_loop_50_k
	test_cpus_l1_loop_50_k
	
.endm


