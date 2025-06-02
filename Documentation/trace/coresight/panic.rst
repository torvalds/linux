===================================================
Using Coresight for Kernel panic and Watchdog reset
===================================================

Introduction
------------
This documentation is about using Linux coresight trace support to
debug kernel panic and watchdog reset scenarios.

Coresight trace during Kernel panic
-----------------------------------
From the coresight driver point of view, addressing the kernel panic
situation has four main requirements.

a. Support for allocation of trace buffer pages from reserved memory area.
   Platform can advertise this using a new device tree property added to
   relevant coresight nodes.

b. Support for stopping coresight blocks at the time of panic

c. Saving required metadata in the specified format

d. Support for reading trace data captured at the time of panic

Allocation of trace buffer pages from reserved RAM
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
A new optional device tree property "memory-region" is added to the
Coresight TMC device nodes, that would give the base address and size of trace
buffer.

Static allocation of trace buffers would ensure that both IOMMU enabled
and disabled cases are handled. Also, platforms that support persistent
RAM will allow users to read trace data in the subsequent boot without
booting the crashdump kernel.

Note:
For ETR sink devices, this reserved region will be used for both trace
capture and trace data retrieval.
For ETF sink devices, internal SRAM would be used for trace capture,
and they would be synced to reserved region for retrieval.


Disabling coresight blocks at the time of panic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
In order to avoid the situation of losing relevant trace data after a
kernel panic, it would be desirable to stop the coresight blocks at the
time of panic.

This can be achieved by configuring the comparator, CTI and sink
devices as below::

           Trigger on panic
    Comparator --->External out --->CTI -->External In---->ETR/ETF stop

Saving metadata at the time of kernel panic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Coresight metadata involves all additional data that are required for a
successful trace decode in addition to the trace data. This involves
ETR/ETF/ETB register snapshot etc.

A new optional device property "memory-region" is added to
the ETR/ETF/ETB device nodes for this.

Reading trace data captured at the time of panic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Trace data captured at the time of panic, can be read from rebooted kernel
or from crashdump kernel using a special device file /dev/crash_tmc_xxx.
This device file is created only when there is a valid crashdata available.

General flow of trace capture and decode incase of kernel panic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1. Enable source and sink on all the cores using the sysfs interface.
   ETR sinks should have trace buffers allocated from reserved memory,
   by selecting "resrv" buffer mode from sysfs.

2. Run relevant tests.

3. On a kernel panic, all coresight blocks are disabled, necessary
   metadata is synced by kernel panic handler.

   System would eventually reboot or boot a crashdump kernel.

4. For  platforms that supports crashdump kernel, raw trace data can be
   dumped using the coresight sysfs interface from the crashdump kernel
   itself. Persistent RAM is not a requirement in this case.

5. For platforms that supports persistent RAM, trace data can be dumped
   using the coresight sysfs interface in the subsequent Linux boot.
   Crashdump kernel is not a requirement in this case. Persistent RAM
   ensures that trace data is intact across reboot.

Coresight trace during Watchdog reset
-------------------------------------
The main difference between addressing the watchdog reset and kernel panic
case are below,

a. Saving coresight metadata need to be taken care by the
   SCP(system control processor) firmware in the specified format,
   instead of kernel.

b. Reserved memory region given by firmware for trace buffer and metadata
   has to be in persistent RAM.
   Note: This is a requirement for watchdog reset case but optional
   in kernel panic case.

Watchdog reset can be supported only on platforms that meet the above
two requirements.

Sample commands for testing a Kernel panic case with ETR sink
-------------------------------------------------------------

1. Boot Linux kernel with "crash_kexec_post_notifiers" added to the kernel
   bootargs. This is mandatory if the user would like to read the tracedata
   from the crashdump kernel.

2. Enable the preloaded ETM configuration::

    #echo 1 > /sys/kernel/config/cs-syscfg/configurations/panicstop/enable

3. Configure CTI using sysfs interface::

    #./cti_setup.sh

    #cat cti_setup.sh


    cd /sys/bus/coresight/devices/

    ap_cti_config () {
      #ETM trig out[0] trigger to Channel 0
      echo 0 4 > channels/trigin_attach
    }

    etf_cti_config () {
      #ETF Flush in trigger from Channel 0
      echo 0 1 > channels/trigout_attach
      echo 1 > channels/trig_filter_enable
    }

    etr_cti_config () {
      #ETR Flush in from Channel 0
      echo 0 1 > channels/trigout_attach
      echo 1 > channels/trig_filter_enable
    }

    ctidevs=`find . -name "cti*"`

    for i in $ctidevs
    do
            cd $i

            connection=`find . -name "ete*"`
            if [ ! -z "$connection" ]
            then
                    echo "AP CTI config for $i"
                    ap_cti_config
            fi

            connection=`find . -name "tmc_etf*"`
            if [ ! -z "$connection" ]
            then
                    echo "ETF CTI config for $i"
                    etf_cti_config
            fi

            connection=`find . -name "tmc_etr*"`
            if [ ! -z "$connection" ]
            then
                    echo "ETR CTI config for $i"
                    etr_cti_config
            fi

            cd ..
    done

Note: CTI connections are SOC specific and hence the above script is
added just for reference.

4. Choose reserved buffer mode for ETR buffer::

    #echo "resrv" > /sys/bus/coresight/devices/tmc_etr0/buf_mode_preferred

5. Enable stop on flush trigger configuration::

    #echo 1 > /sys/bus/coresight/devices/tmc_etr0/stop_on_flush

6. Start Coresight tracing on cores 1 and 2 using sysfs interface

7. Run some application on core 1::

    #taskset -c 1 dd if=/dev/urandom of=/dev/null &

8. Invoke kernel panic on core 2::

    #echo 1 > /proc/sys/kernel/panic
    #taskset -c 2 echo c > /proc/sysrq-trigger

9. From rebooted kernel or crashdump kernel, read crashdata::

    #dd if=/dev/crash_tmc_etr0 of=/trace/cstrace.bin

10. Run opencsd decoder tools/scripts to generate the instruction trace.

Sample instruction trace dump
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Core1 dump::

    A                                  etm4_enable_hw: ffff800008ae1dd4
    CONTEXT EL2                        etm4_enable_hw: ffff800008ae1dd4
    I                                  etm4_enable_hw: ffff800008ae1dd4:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1dd8:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1ddc:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1de0:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1de4:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1de8:
    d503233f   paciasp
    I                                  etm4_enable_hw: ffff800008ae1dec:
    a9be7bfd   stp     x29, x30, [sp, #-32]!
    I                                  etm4_enable_hw: ffff800008ae1df0:
    910003fd   mov     x29, sp
    I                                  etm4_enable_hw: ffff800008ae1df4:
    a90153f3   stp     x19, x20, [sp, #16]
    I                                  etm4_enable_hw: ffff800008ae1df8:
    2a0003f4   mov     w20, w0
    I                                  etm4_enable_hw: ffff800008ae1dfc:
    900085b3   adrp    x19, ffff800009b95000 <reserved_mem+0xc48>
    I                                  etm4_enable_hw: ffff800008ae1e00:
    910f4273   add     x19, x19, #0x3d0
    I                                  etm4_enable_hw: ffff800008ae1e04:
    f8747a60   ldr     x0, [x19, x20, lsl #3]
    E                                  etm4_enable_hw: ffff800008ae1e08:
    b4000140   cbz     x0, ffff800008ae1e30 <etm4_starting_cpu+0x50>
    I    149.039572921                 etm4_enable_hw: ffff800008ae1e30:
    a94153f3   ldp     x19, x20, [sp, #16]
    I    149.039572921                 etm4_enable_hw: ffff800008ae1e34:
    52800000   mov     w0, #0x0                        // #0
    I    149.039572921                 etm4_enable_hw: ffff800008ae1e38:
    a8c27bfd   ldp     x29, x30, [sp], #32

    ..snip

        149.052324811           chacha_block_generic: ffff800008642d80:
    9100a3e0   add     x0,
    I    149.052324811           chacha_block_generic: ffff800008642d84:
    b86178a2   ldr     w2, [x5, x1, lsl #2]
    I    149.052324811           chacha_block_generic: ffff800008642d88:
    8b010803   add     x3, x0, x1, lsl #2
    I    149.052324811           chacha_block_generic: ffff800008642d8c:
    b85fc063   ldur    w3, [x3, #-4]
    I    149.052324811           chacha_block_generic: ffff800008642d90:
    0b030042   add     w2, w2, w3
    I    149.052324811           chacha_block_generic: ffff800008642d94:
    b8217882   str     w2, [x4, x1, lsl #2]
    I    149.052324811           chacha_block_generic: ffff800008642d98:
    91000421   add     x1, x1, #0x1
    I    149.052324811           chacha_block_generic: ffff800008642d9c:
    f100443f   cmp     x1, #0x11


Core 2 dump::

    A                                  etm4_enable_hw: ffff800008ae1dd4
    CONTEXT EL2                        etm4_enable_hw: ffff800008ae1dd4
    I                                  etm4_enable_hw: ffff800008ae1dd4:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1dd8:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1ddc:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1de0:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1de4:
    d503201f   nop
    I                                  etm4_enable_hw: ffff800008ae1de8:
    d503233f   paciasp
    I                                  etm4_enable_hw: ffff800008ae1dec:
    a9be7bfd   stp     x29, x30, [sp, #-32]!
    I                                  etm4_enable_hw: ffff800008ae1df0:
    910003fd   mov     x29, sp
    I                                  etm4_enable_hw: ffff800008ae1df4:
    a90153f3   stp     x19, x20, [sp, #16]
    I                                  etm4_enable_hw: ffff800008ae1df8:
    2a0003f4   mov     w20, w0
    I                                  etm4_enable_hw: ffff800008ae1dfc:
    900085b3   adrp    x19, ffff800009b95000 <reserved_mem+0xc48>
    I                                  etm4_enable_hw: ffff800008ae1e00:
    910f4273   add     x19, x19, #0x3d0
    I                                  etm4_enable_hw: ffff800008ae1e04:
    f8747a60   ldr     x0, [x19, x20, lsl #3]
    E                                  etm4_enable_hw: ffff800008ae1e08:
    b4000140   cbz     x0, ffff800008ae1e30 <etm4_starting_cpu+0x50>
    I    149.046243445                 etm4_enable_hw: ffff800008ae1e30:
    a94153f3   ldp     x19, x20, [sp, #16]
    I    149.046243445                 etm4_enable_hw: ffff800008ae1e34:
    52800000   mov     w0, #0x0                        // #0
    I    149.046243445                 etm4_enable_hw: ffff800008ae1e38:
    a8c27bfd   ldp     x29, x30, [sp], #32
    I    149.046243445                 etm4_enable_hw: ffff800008ae1e3c:
    d50323bf   autiasp
    E    149.046243445                 etm4_enable_hw: ffff800008ae1e40:
    d65f03c0   ret
    A                                ete_sysreg_write: ffff800008adfa18

    ..snip

    I     149.05422547                          panic: ffff800008096300:
    a90363f7   stp     x23, x24, [sp, #48]
    I     149.05422547                          panic: ffff800008096304:
    6b00003f   cmp     w1, w0
    I     149.05422547                          panic: ffff800008096308:
    3a411804   ccmn    w0, #0x1, #0x4, ne  // ne = any
    N     149.05422547                          panic: ffff80000809630c:
    540001e0   b.eq    ffff800008096348 <panic+0xe0>  // b.none
    I     149.05422547                          panic: ffff800008096310:
    f90023f9   str     x25, [sp, #64]
    E     149.05422547                          panic: ffff800008096314:
    97fe44ef   bl      ffff8000080276d0 <panic_smp_self_stop>
    A                                           panic: ffff80000809634c
    I     149.05422547                          panic: ffff80000809634c:
    910102d5   add     x21, x22, #0x40
    I     149.05422547                          panic: ffff800008096350:
    52800020   mov     w0, #0x1                        // #1
    E     149.05422547                          panic: ffff800008096354:
    94166b8b   bl      ffff800008631180 <bust_spinlocks>
    N    149.054225518                 bust_spinlocks: ffff800008631180:
    340000c0   cbz     w0, ffff800008631198 <bust_spinlocks+0x18>
    I    149.054225518                 bust_spinlocks: ffff800008631184:
    f000a321   adrp    x1, ffff800009a98000 <pbufs.0+0xbb8>
    I    149.054225518                 bust_spinlocks: ffff800008631188:
    b9405c20   ldr     w0, [x1, #92]
    I    149.054225518                 bust_spinlocks: ffff80000863118c:
    11000400   add     w0, w0, #0x1
    I    149.054225518                 bust_spinlocks: ffff800008631190:
    b9005c20   str     w0, [x1, #92]
    E    149.054225518                 bust_spinlocks: ffff800008631194:
    d65f03c0   ret
    A                                           panic: ffff800008096358

Perf based testing
------------------

Starting perf session
~~~~~~~~~~~~~~~~~~~~~
ETF::

    perf record -e cs_etm/panicstop,@tmc_etf1/ -C 1
    perf record -e cs_etm/panicstop,@tmc_etf2/ -C 2

ETR::

    perf record -e cs_etm/panicstop,@tmc_etr0/ -C 1,2

Reading trace data after panic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Same sysfs based method explained above can be used to retrieve and
decode the trace data after the reboot on kernel panic.
