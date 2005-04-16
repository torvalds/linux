#include <asm/hardware.h>

@ Code for DMA with the 1772 fdc
.text


  .global fdc1772_dataaddr
fdc1772_fiqdata:
@ Number of bytes left to DMA
  .global fdc1772_bytestogo
fdc1772_bytestogo:
  .word 0
@ Place to put/get data from in DMA
  .global fdc1772_dataaddr
fdc1772_dataaddr:
  .word 0
  
  .global fdc1772_fdc_int_done
fdc1772_fdc_int_done:
  .word 0
  .global fdc1772_comendstatus
fdc1772_comendstatus:
  .word 0

@ We hang this off DMA channel 1
    .global fdc1772_comendhandler
fdc1772_comendhandler:
  mov      r8,#IOC_BASE
  ldrb     r9,[r8,#0x34]    @ IOC FIQ status
  tst      r9,#2
  subeqs   pc,r14,#4        @ should I leave a space here
  orr      r9,r8,#0x10000   @ FDC base
  adr      r8,fdc1772_fdc_int_done
  ldrb     r10,[r9,#0]  @ FDC status
  mov      r9,#1        @ Got a FIQ flag
  stmia    r8,{r9,r10}
  subs     pc,r14,#4


    .global fdc1772_dma_read
fdc1772_dma_read:
  mov      r8,#IOC_BASE
  ldrb     r9,[r8,#0x34]    @ IOC FIQ status
  tst      r9,#1
  beq      fdc1772_dma_read_notours
  orr      r8,r8,#0x10000   @ FDC base
  ldrb     r10,[r8,#0xc]   @ Read from FDC data reg (also clears interrupt)
  ldmia    r11,{r8,r9}
  subs     r8,r8,#1        @ One less byte to go
  @ If there was somewhere for this data to go then store it and update pointers
  strplb   r10,[r9],#1     @ Store the data and increment the pointer
  stmplia  r11,{r8,r9}     @ Update count/pointers
  @ Handle any other interrupts if there are any
fdc1772_dma_read_notours:
  @ Cant branch because this code has been copied down to the FIQ vector
  ldr pc,[pc,#-4]
  .word fdc1772_comendhandler
  .global fdc1772_dma_read_end
fdc1772_dma_read_end:

    .global fdc1772_dma_write
fdc1772_dma_write:
  mov      r8,#IOC_BASE
  ldrb     r9,[r8,#0x34]    @ IOC FIQ status
  tst      r9,#1
  beq      fdc1772_dma_write_notours
  orr      r8,r8,#0x10000   @ FDC base
  ldmia    r11,{r9,r10}
  subs     r9,r9,#1        @ One less byte to go
  @ If there really is some data then get it, store it and update count
  ldrplb   r12,[r10],#1
  strplb   r12,[r8,#0xc]   @ write it to FDC data reg
  stmplia  r11,{r9,r10}    @ Update count and pointer - should clear interrupt
  @ Handle any other interrupts
fdc1772_dma_write_notours:
  @ Cant branch because this code has been copied down to the FIQ vector
  ldr pc,[pc,#-4]
  .word fdc1772_comendhandler

  .global fdc1772_dma_write_end
fdc1772_dma_write_end:
  

@ Setup the FIQ R11 to point to the data and store the count, address
@ for this dma
@ R0=count
@ R1=address
  .global fdc1772_setupdma
fdc1772_setupdma:
	@ The big job is flipping in and out of FIQ mode
	adr	r2,fdc1772_fiqdata	@ This is what we really came here for
  stmia  r2,{r0,r1}
	mov	r3, pc
	teqp	pc,#0x0c000001	@ Disable FIQs, IRQs and switch to FIQ mode
	mov	r0,r0      	@ NOP
	mov r11,r2
	teqp	r3,#0		@ Normal mode
	mov	r0,r0		@ NOP
  mov pc,r14

