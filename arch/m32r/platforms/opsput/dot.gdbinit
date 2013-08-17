# .gdbinit file
# $Id: dot.gdbinit,v 1.1 2004/07/27 06:54:20 sakugawa Exp $

# setting
set width 0d70
set radix 0d16
set height 0
debug_chaos

# clk xin:cpu:bus=1:8:1
define clock_init_on_181
  set *(unsigned long *)0x00ef400c = 0x2
  set *(unsigned long *)0x00ef4004 = 0x1
  shell sleep 0.1
  set *(unsigned long *)0x00ef4000 = 0x101
end
# clk xin:cpu:bus=1:8:2
define clock_init_on_182
  set *(unsigned long *)0x00ef400c = 0x1
  set *(unsigned long *)0x00ef4004 = 0x1
  shell sleep 0.1
  set *(unsigned long *)0x00ef4000 = 0x101
end

# clk xin:cpu:bus=1:8:4
define clock_init_on_184
  set *(unsigned long *)0x00ef400c = 0x0
  set *(unsigned long *)0x00ef4004 = 0x1
  shell sleep 0.1
  set *(unsigned long *)0x00ef4000 = 0x101
end

# clk xin:cpu:bus=1:1:1
define clock_init_off
  shell sleep 0.1
  set *(unsigned long *)0x00ef4000 = 0x0
  shell sleep 0.1
  set *(unsigned long *)0x00ef4004 = 0x0
  shell sleep 0.1
  set *(unsigned long *)0x00ef400c = 0x0
end

define tlb_init
  set $tlbbase = 0xfe000000
  set *(unsigned long *)($tlbbase + 0x04) = 0x0
  set *(unsigned long *)($tlbbase + 0x0c) = 0x0
  set *(unsigned long *)($tlbbase + 0x14) = 0x0
  set *(unsigned long *)($tlbbase + 0x1c) = 0x0
  set *(unsigned long *)($tlbbase + 0x24) = 0x0
  set *(unsigned long *)($tlbbase + 0x2c) = 0x0
  set *(unsigned long *)($tlbbase + 0x34) = 0x0
  set *(unsigned long *)($tlbbase + 0x3c) = 0x0
  set *(unsigned long *)($tlbbase + 0x44) = 0x0
  set *(unsigned long *)($tlbbase + 0x4c) = 0x0
  set *(unsigned long *)($tlbbase + 0x54) = 0x0
  set *(unsigned long *)($tlbbase + 0x5c) = 0x0
  set *(unsigned long *)($tlbbase + 0x64) = 0x0
  set *(unsigned long *)($tlbbase + 0x6c) = 0x0
  set *(unsigned long *)($tlbbase + 0x74) = 0x0
  set *(unsigned long *)($tlbbase + 0x7c) = 0x0
  set *(unsigned long *)($tlbbase + 0x84) = 0x0
  set *(unsigned long *)($tlbbase + 0x8c) = 0x0
  set *(unsigned long *)($tlbbase + 0x94) = 0x0
  set *(unsigned long *)($tlbbase + 0x9c) = 0x0
  set *(unsigned long *)($tlbbase + 0xa4) = 0x0
  set *(unsigned long *)($tlbbase + 0xac) = 0x0
  set *(unsigned long *)($tlbbase + 0xb4) = 0x0
  set *(unsigned long *)($tlbbase + 0xbc) = 0x0
  set *(unsigned long *)($tlbbase + 0xc4) = 0x0
  set *(unsigned long *)($tlbbase + 0xcc) = 0x0
  set *(unsigned long *)($tlbbase + 0xd4) = 0x0
  set *(unsigned long *)($tlbbase + 0xdc) = 0x0
  set *(unsigned long *)($tlbbase + 0xe4) = 0x0
  set *(unsigned long *)($tlbbase + 0xec) = 0x0
  set *(unsigned long *)($tlbbase + 0xf4) = 0x0
  set *(unsigned long *)($tlbbase + 0xfc) = 0x0
  set $tlbbase = 0xfe000800
  set *(unsigned long *)($tlbbase + 0x04) = 0x0
  set *(unsigned long *)($tlbbase + 0x0c) = 0x0
  set *(unsigned long *)($tlbbase + 0x14) = 0x0
  set *(unsigned long *)($tlbbase + 0x1c) = 0x0
  set *(unsigned long *)($tlbbase + 0x24) = 0x0
  set *(unsigned long *)($tlbbase + 0x2c) = 0x0
  set *(unsigned long *)($tlbbase + 0x34) = 0x0
  set *(unsigned long *)($tlbbase + 0x3c) = 0x0
  set *(unsigned long *)($tlbbase + 0x44) = 0x0
  set *(unsigned long *)($tlbbase + 0x4c) = 0x0
  set *(unsigned long *)($tlbbase + 0x54) = 0x0
  set *(unsigned long *)($tlbbase + 0x5c) = 0x0
  set *(unsigned long *)($tlbbase + 0x64) = 0x0
  set *(unsigned long *)($tlbbase + 0x6c) = 0x0
  set *(unsigned long *)($tlbbase + 0x74) = 0x0
  set *(unsigned long *)($tlbbase + 0x7c) = 0x0
  set *(unsigned long *)($tlbbase + 0x84) = 0x0
  set *(unsigned long *)($tlbbase + 0x8c) = 0x0
  set *(unsigned long *)($tlbbase + 0x94) = 0x0
  set *(unsigned long *)($tlbbase + 0x9c) = 0x0
  set *(unsigned long *)($tlbbase + 0xa4) = 0x0
  set *(unsigned long *)($tlbbase + 0xac) = 0x0
  set *(unsigned long *)($tlbbase + 0xb4) = 0x0
  set *(unsigned long *)($tlbbase + 0xbc) = 0x0
  set *(unsigned long *)($tlbbase + 0xc4) = 0x0
  set *(unsigned long *)($tlbbase + 0xcc) = 0x0
  set *(unsigned long *)($tlbbase + 0xd4) = 0x0
  set *(unsigned long *)($tlbbase + 0xdc) = 0x0
  set *(unsigned long *)($tlbbase + 0xe4) = 0x0
  set *(unsigned long *)($tlbbase + 0xec) = 0x0
  set *(unsigned long *)($tlbbase + 0xf4) = 0x0
  set *(unsigned long *)($tlbbase + 0xfc) = 0x0
end

define load_modules
  use_debug_dma
  load
end

# Set kernel parameters
define set_kernel_parameters
  set $param = (void*)0x88001000
  # INITRD_START
#  set *(unsigned long *)($param + 0x0010) = 0x08300000
  # INITRD_SIZE
#  set *(unsigned long *)($param + 0x0014) = 0x00400000
  # M32R_CPUCLK
  set *(unsigned long *)($param + 0x0018) = 0d200000000
  # M32R_BUSCLK
  set *(unsigned long *)($param + 0x001c) = 0d50000000
#  set *(unsigned long *)($param + 0x001c) = 0d25000000

  # M32R_TIMER_DIVIDE
  set *(unsigned long *)($param + 0x0020) = 0d128

  set {char[0x200]}($param + 0x100) = "console=ttyS0,115200n8x console=tty1 \
  root=/dev/nfsroot \
  nfsroot=192.168.0.1:/project/m32r-linux/export/root.2.6 \
  nfsaddrs=192.168.0.101:192.168.0.1:192.168.0.1:255.255.255.0:mappi001 \
  mem=16m \0"
end

define boot
  set_kernel_parameters
  set $pc=0x88002000
  set $fp=0
  set $evb=0x88000000
  si
  c
end

# Show TLB entries
define show_tlb_entries
  set $i = 0
  set $addr = $arg0
  use_mon_code
  while ($i < 0d32 )
    set $tlb_tag = *(unsigned long*)$addr
    set $tlb_data = *(unsigned long*)($addr + 4)
    printf " [%2d] 0x%08lx : 0x%08lx - 0x%08lx\n", $i, $addr, $tlb_tag, $tlb_data
    set $i = $i + 1
    set $addr = $addr + 8
  end
#  use_debug_dma
end
define itlb
  set $itlb=0xfe000000
  show_tlb_entries $itlb
end
define dtlb
  set $dtlb=0xfe000800
  show_tlb_entries $dtlb
end

define show_regs
  printf " R0[%08lx]   R1[%08lx]   R2[%08lx]   R3[%08lx]\n",$r0,$r1,$r2,$r3
  printf " R4[%08lx]   R5[%08lx]   R6[%08lx]   R7[%08lx]\n",$r4,$r5,$r6,$r7
  printf " R8[%08lx]   R9[%08lx]  R10[%08lx]  R11[%08lx]\n",$r8,$r9,$r10,$r11
  printf "R12[%08lx]   FP[%08lx]   LR[%08lx]   SP[%08lx]\n",$r12,$fp,$lr,$sp
  printf "PSW[%08lx]  CBR[%08lx]  SPI[%08lx]  SPU[%08lx]\n",$psw,$cbr,$spi,$spu
  printf "BPC[%08lx]   PC[%08lx] ACCL[%08lx] ACCH[%08lx]\n",$bpc,$pc,$accl,$acch
  printf "EVB[%08lx]\n",$evb
end

define restart
  sdireset
  sdireset
  en 1
  set $pc=0x0
  c
  tlb_init
  setup
  load_modules
  boot
end

define setup
  debug_chaos
# Clock
#  shell sleep 0.1
#  clock_init_off
#  shell sleep 1
#  clock_init_on_182
#  shell sleep 0.1
# SDRAM
  set *(unsigned long *)0xa0ef6004 = 0x0001053f
  set *(unsigned long *)0xa0ef6028 = 0x00031102
end

sdireset
sdireset
file vmlinux
target m32rsdi
set $pc=0x0
b *0x30000
c
dis 1
setup
tlb_init
load_modules
boot
