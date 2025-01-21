.. SPDX-License-Identifier: GPL-2.0+

===============================================
Xilinx ZynqMP Ultrascale+ DisplayPort Subsystem
===============================================

This subsystem handles DisplayPort video and audio output on the ZynqMP. It
supports in-memory framebuffers with the DisplayPort DMA controller
(xilinx-dpdma), as well as "live" video and audio from the programmable logic
(PL). This subsystem can perform several transformations, including color space
conversion, alpha blending, and audio mixing, although not all features are
currently supported.

debugfs
-------

To support debugging and compliance testing, several test modes can be enabled
though debugfs. The following files in /sys/kernel/debug/dri/X/DP-1/test/
control the DisplayPort test modes:

active:
        Writing a 1 to this file will activate test mode, and writing a 0 will
        deactivate test mode. Writing a 1 or 0 when the test mode is already
        active/inactive will re-activate/re-deactivate test mode. When test
        mode is inactive, changes made to other files will have no (immediate)
        effect, although the settings will be saved for when test mode is
        activated. When test mode is active, changes made to other files will
        apply immediately.

custom:
        Custom test pattern value

downspread:
        Enable/disable clock downspreading (spread-spectrum clocking) by
        writing 1/0

enhanced:
        Enable/disable enhanced framing

ignore_aux_errors:
        Ignore AUX errors when set to 1. Writes to this file take effect
        immediately (regardless of whether test mode is active) and affect all
        AUX transfers.

ignore_hpd:
        Ignore hotplug events (such as cable removals or monitor link
        retraining requests) when set to 1. Writes to this file take effect
        immediately (regardless of whether test mode is active).

laneX_preemphasis:
        Preemphasis from 0 (lowest) to 2 (highest) for lane X

laneX_swing:
        Voltage swing from 0 (lowest) to 3 (highest) for lane X

lanes:
        Number of lanes to use (1, 2, or 4)

pattern:
        Test pattern. May be one of:

                video
                        Use regular video input

                symbol-error
                        Symbol error measurement pattern

                prbs7
                        Output of the PRBS7 (x^7 + x^6 + 1) polynomial

                80bit-custom
                        A custom 80-bit pattern

                cp2520
                        HBR2 compliance eye pattern

                tps1
                        Link training symbol pattern TPS1 (/D10.2/)

                tps2
                        Link training symbol pattern TPS2

                tps3
                        Link training symbol pattern TPS3 (for HBR2)

rate:
        Rate in hertz. One of

                * 5400000000 (HBR2)
                * 2700000000 (HBR)
                * 1620000000 (RBR)

You can dump the displayport test settings with the following command::

        for prop in /sys/kernel/debug/dri/1/DP-1/test/*; do
                printf '%-17s ' ${prop##*/}
                if [ ${prop##*/} = custom ]; then
                        hexdump -C $prop | head -1
                else
                        cat $prop
                fi
        done

The output could look something like::

        active            1
        custom            00000000  00 00 00 00 00 00 00 00  00 00                    |..........|
        downspread        0
        enhanced          1
        ignore_aux_errors 1
        ignore_hpd        1
        lane0_preemphasis 0
        lane0_swing       3
        lane1_preemphasis 0
        lane1_swing       3
        lanes             2
        pattern           prbs7
        rate              1620000000

The recommended test procedure is to connect the board to a monitor,
configure test mode, activate test mode, and then disconnect the cable
and connect it to your test equipment of choice. For example, one
sequence of commands could be::

        echo 1 > /sys/kernel/debug/dri/1/DP-1/test/enhanced
        echo tps1 > /sys/kernel/debug/dri/1/DP-1/test/pattern
        echo 1620000000 > /sys/kernel/debug/dri/1/DP-1/test/rate
        echo 1 > /sys/kernel/debug/dri/1/DP-1/test/ignore_aux_errors
        echo 1 > /sys/kernel/debug/dri/1/DP-1/test/ignore_hpd
        echo 1 > /sys/kernel/debug/dri/1/DP-1/test/active

at which point the cable could be disconnected from the monitor.

Internals
---------

.. kernel-doc:: drivers/gpu/drm/xlnx/zynqmp_disp.h

.. kernel-doc:: drivers/gpu/drm/xlnx/zynqmp_dpsub.h

.. kernel-doc:: drivers/gpu/drm/xlnx/zynqmp_kms.h

.. kernel-doc:: drivers/gpu/drm/xlnx/zynqmp_disp.c

.. kernel-doc:: drivers/gpu/drm/xlnx/zynqmp_dp.c

.. kernel-doc:: drivers/gpu/drm/xlnx/zynqmp_dpsub.c

.. kernel-doc:: drivers/gpu/drm/xlnx/zynqmp_kms.c
