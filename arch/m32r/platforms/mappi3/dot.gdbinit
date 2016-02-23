# .gdbinit file
# $Id: dot.gdbinit,v 1.1 2005/04/11 02:21:08 sakugawa Exp $

# setting
set width 0d70
set radix 0d16
use_debug_dma

# Initialize SDRAM controller for Mappi
define sdram_init
  # SDIR0
  set *(unsigned long *)0x00ef6008 = 0x00000182
  # SDIR1
  set *(unsigned long *)0x00ef600c = 0x00000001
  # Initialize wait
  shell sleep 0.1
  # MOD
  set *(unsigned long *)0x00ef602c = 0x00000020
  set *(unsigned long *)0x00ef604c = 0x00000020
  # TR
  set *(unsigned long *)0x00ef6028 = 0x00051502
  set *(unsigned long *)0x00ef6048 = 0x00051502
  # ADR
  set *(unsigned long *)0x00ef6020 = 0x08000004
  set *(unsigned long *)0x00ef6040 = 0x0c000004
  # AutoRef On
  set *(unsigned long *)0x00ef6004 = 0x00010517
  # Access enable
  set *(unsigned long *)0x00ef6024 = 0x00000001
  set *(unsigned long *)0x00ef6044 = 0x00000001
end

# Initialize LAN controller for Mappi
define lanc_init
  # Set BSEL4
  #set *(unsigned long *)0x00ef5004 = 0x0fff330f
  #set *(unsigned long *)0x00ef5004 = 0x01113301

#  set *(unsigned long *)0x00ef5004 = 0x02011101
#  set *(unsigned long *)0x00ef5004 = 0x04441104
end

define clock_init
  set *(unsigned long *)0x00ef4010 = 2
  set *(unsigned long *)0x00ef4014 = 2
  set *(unsigned long *)0x00ef4020 = 3
  set *(unsigned long *)0x00ef4024 = 3
  set *(unsigned long *)0x00ef4004 = 0x7
#  shell sleep 0.1
#  set *(unsigned long *)0x00ef4004 = 0x5
  shell sleep 0.1
  set *(unsigned long *)0x00ef4008 = 0x0200
end

define port_init
  set $sfrbase = 0x00ef0000
  set *(unsigned short *)0x00ef1060 = 0x5555
  set *(unsigned short *)0x00ef1062 = 0x5555
  set *(unsigned short *)0x00ef1064 = 0x5555
  set *(unsigned short *)0x00ef1066 = 0x5555
  set *(unsigned short *)0x00ef1068 = 0x5555
  set *(unsigned short *)0x00ef106a = 0x0000
  set *(unsigned short *)0x00ef106e = 0x5555
  set *(unsigned short *)0x00ef1070 = 0x5555
end

# MMU enable
define mmu_enable
  set $evb=0x88000000
  set *(unsigned long *)0xffff0024=1
end

# MMU disable
define mmu_disable
  set $evb=0
  set *(unsigned long *)0xffff0024=0
end

# Show TLB entries
define show_tlb_entries
  set $i = 0
  set $addr = $arg0
  while ($i < 0d16 )
    set $tlb_tag = *(unsigned long*)$addr
    set $tlb_data = *(unsigned long*)($addr + 4)
    printf " [%2d] 0x%08lx : 0x%08lx - 0x%08lx\n", $i, $addr, $tlb_tag, $tlb_data
    set $i = $i + 1
    set $addr = $addr + 8
  end
end
define itlb
  set $itlb=0xfe000000
  show_tlb_entries $itlb
end
define dtlb
  set $dtlb=0xfe000800
  show_tlb_entries $dtlb
end

# Cache ON
define set_cache_type
  set $mctype = (void*)0xfffffff8
# chaos
# set *(unsigned long *)($mctype) = 0x0000c000
# m32102 i-cache only
  set *(unsigned long *)($mctype) = 0x00008000
# m32102 d-cache only
#  set *(unsigned long *)($mctype) = 0x00004000
end
define cache_on
  set $param = (void*)0x08001000
  set *(unsigned long *)($param) = 0x60ff6102
end


# Show current task structure
define show_current
  set $current = $spi & 0xffffe000
  printf "$current=0x%08lX\n",$current
  print *(struct task_struct *)$current
end

# Show user assigned task structure
define show_task
  set $task = $arg0 & 0xffffe000
  printf "$task=0x%08lX\n",$task
  print *(struct task_struct *)$task
end
document show_task
  Show user assigned task structure
  arg0 : task structure address
end

# Show M32R registers
define show_regs
  printf " R0[0x%08lX]   R1[0x%08lX]   R2[0x%08lX]   R3[0x%08lX]\n",$r0,$r1,$r2,$r3
  printf " R4[0x%08lX]   R5[0x%08lX]   R6[0x%08lX]   R7[0x%08lX]\n",$r4,$r5,$r6,$r7
  printf " R8[0x%08lX]   R9[0x%08lX]  R10[0x%08lX]  R11[0x%08lX]\n",$r8,$r9,$r10,$r11
  printf "R12[0x%08lX]   FP[0x%08lX]   LR[0x%08lX]   SP[0x%08lX]\n",$r12,$fp,$lr,$sp
  printf "PSW[0x%08lX]  CBR[0x%08lX]  SPI[0x%08lX]  SPU[0x%08lX]\n",$psw,$cbr,$spi,$spu
  printf "BPC[0x%08lX]   PC[0x%08lX] ACCL[0x%08lX] ACCH[0x%08lX]\n",$bpc,$pc,$accl,$acch
  printf "EVB[0x%08lX]\n",$evb

  set $mests = *(unsigned long *)0xffff000c
  set $mdeva = *(unsigned long *)0xffff0010
  printf "MESTS[0x%08lX] MDEVA[0x%08lX]\n",$mests,$mdeva
end


# Setup all
define setup
  clock_init
  shell sleep 0.1
  port_init
  sdram_init
#  lanc_init
#  dispc_init
#  set $evb=0x08000000
end

# Load modules
define load_modules
  use_debug_dma
  load
#  load busybox.mot
end

# Set kernel parameters
define set_kernel_parameters
  set $param = (void*)0x08001000

  ## MOUNT_ROOT_RDONLY
  set {long}($param+0x00)=0
  ## RAMDISK_FLAGS
  #set {long}($param+0x04)=0
  ## ORIG_ROOT_DEV
  #set {long}($param+0x08)=0x00000100
  ## LOADER_TYPE
  #set {long}($param+0x0C)=0
  ## INITRD_START
  set {long}($param+0x10)=0x082a0000
  ## INITRD_SIZE
  set {long}($param+0x14)=0d6200000

  # M32R_CPUCLK
  set *(unsigned long *)($param + 0x0018) = 0d100000000
  # M32R_BUSCLK
  set *(unsigned long *)($param + 0x001c) = 0d50000000
  # M32R_TIMER_DIVIDE
  set *(unsigned long *)($param + 0x0020) = 0d128


 set {char[0x200]}($param + 0x100) = "console=ttyS0,115200n8x root=/dev/nfsroot nfsroot=192.168.0.1:/project/m32r-linux/export/root.2.6_04 nfsaddrs=192.168.0.102:192.168.0.1:192.168.0.1:255.255.255.0:mappi: \0"


end

# Boot
define boot
  set_kernel_parameters
  debug_chaos
  set *(unsigned long *)0x00f00000=0x08002000
  set $pc=0x08002000
  set $fp=0
  del b
  si
end

# Restart
define restart
  sdireset
  sdireset
  setup
  load_modules
  boot
end

sdireset
sdireset
file vmlinux
target m32rsdi

restart
boot
