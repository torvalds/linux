@ Read/Write DMA code for the ST506/MFM hard drive controllers on the A400 Acorn Archimedes
@   motherboard and on ST506 expansion podules.
@ (c) David Alan Gilbert (linux@treblig.org) 1996-1999

#include <asm/assembler.h>
 
hdc63463_irqdata:
@ Controller base address
  .global hdc63463_baseaddress
hdc63463_baseaddress:
  .word 0

  .global hdc63463_irqpolladdress
hdc63463_irqpolladdress:
  .word 0
 
  .global hdc63463_irqpollmask
hdc63463_irqpollmask:
  .word 0

@ where to read/write data  from the kernel data space
  .global hdc63463_dataptr
hdc63463_dataptr:
  .word 0

@ Number of bytes left to transfer
  .global hdc63463_dataleft
hdc63463_dataleft:
  .word 0

@ -------------------------------------------------------------------------
@ hdc63463_writedma: DMA from host to controller
@  internal reg usage: r0=hdc base address, r1=irq poll address, r2=poll mask
@                      r3=data ptr, r4=data left, r5,r6=temporary
  .global hdc63463_writedma
hdc63463_writedma:
  stmfd sp!,{r4-r7}
  adr r5,hdc63463_irqdata
  ldmia r5,{r0,r1,r2,r3,r4}

writedma_again:

  @ test number of remaining bytes to transfer
  cmp r4,#0
  beq writedma_end
  bmi writedma_end

  @ Check the hdc is interrupting
  ldrb r5,[r1,#0]
  tst r5,r2
  beq writedma_end

  @ Transfer a block of upto 256 bytes
  cmp r4,#256
  movlt r7,r4
  movge r7,#256

  @ Check the hdc is still busy and command has not ended and no errors
  ldr r5,[r0,#32]     @ Status reg - 16 bit - its the top few bits which are status
  @ think we should continue DMA until it drops busy - perhaps this was
  @ the main problem with corrected errors causing a hang
  @tst r5,#0x3c00        @ Test for things which should be off
  @bne writedma_end
  and r5,r5,#0x8000        @ This is test for things which should be on: Busy
  cmp r5,#0x8000
  bne writedma_end 

  @ Bytes remaining at end
  sub r4,r4,r7

  @ HDC Write register location
  add r0,r0,#32+8

writedma_loop:
  @ OK - pretty sure we should be doing this

  ldr r5,[r3],#4          @ Get a word to be written
  @ get bottom half to be sent first
  mov r6,r5,lsl#16        @ Separate the first 2 bytes
  orr r2,r6,r6,lsr #16    @ Duplicate them in the bottom half of the word
  @ now the top half
  mov r6,r5,lsr#16        @ Get 2nd 2 bytes
  orr r6,r6,r6,lsl#16     @ Duplicate
  @str r6,[r0]       @ to hdc
  stmia r0,{r2,r6}
  subs r7,r7,#4           @ Dec. number of bytes left
  bne writedma_loop

  @ If we were too slow we had better go through again - DAG - took out with new interrupt routine
  @ sub r0,r0,#32+8
  @ adr r2,hdc63463_irqdata
  @ ldr r2,[r2,#8]
  @ b writedma_again

writedma_end:
  adr r5,hdc63463_irqdata+12
  stmia r5,{r3,r4}
  ldmfd sp!,{r4-r7}
  RETINSTR(mov,pc,lr)

@ -------------------------------------------------------------------------
@ hdc63463_readdma: DMA from controller to host
@  internal reg usage: r0=hdc base address, r1=irq poll address, r2=poll mask
@                      r3=data ptr, r4=data left, r5,r6=temporary
  .global hdc63463_readdma
hdc63463_readdma:
  stmfd sp!,{r4-r7}
  adr r5,hdc63463_irqdata
  ldmia r5,{r0,r1,r2,r3,r4}

readdma_again:
  @ test number of remaining bytes to transfer
  cmp r4,#0
  beq readdma_end
  bmi readdma_end

  @ Check the hdc is interrupting
  ldrb r5,[r1,#0]
  tst r5,r2
  beq readdma_end

  @ Check the hdc is still busy and command has not ended and no errors
  ldr r5,[r0,#32]     @ Status reg - 16 bit - its the top few bits which are status
  @ think we should continue DMA until it drops busy - perhaps this was
  @ the main problem with corrected errors causing a hang
  @tst r5,#0x3c00      @ Test for things which should be off
  @bne readdma_end
  and r5,r5,#0x8000        @ This is test for things which should be on: Busy
  cmp r5,#0x8000
  bne readdma_end 

  @ Transfer a block of upto 256 bytes
  cmp r4,#256
  movlt r7,r4
  movge r7,#256

  @ Bytes remaining at end
  sub r4,r4,r7

  @ Set a pointer to the data register in the HDC
  add r0,r0,#8
readdma_loop:
  @ OK - pretty sure we should be doing this
  ldmia r0,{r5,r6}
  mov r5,r5,lsl#16
  mov r6,r6,lsl#16
  orr r6,r6,r5,lsr #16
  str r6,[r3],#4
  subs r7,r7,#4        @ Decrement bytes to go
  bne readdma_loop

  @ Try reading multiple blocks - if this was fast enough then I do not think
  @ this should help - NO taken out DAG - new interrupt handler has
  @ non-consecutive memory blocks
  @ sub r0,r0,#8
  @ b readdma_again

readdma_end:
  adr r5,hdc63463_irqdata+12
  stmia r5,{r3,r4}
  ldmfd sp!,{r4-r7}
  RETINSTR(mov,pc,lr)
