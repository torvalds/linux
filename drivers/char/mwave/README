Module options
--------------

The mwave module takes the following options.  Note that these options
are not saved by the BIOS and so do not persist after unload and reload.

  mwave_debug=value, where value is bitwise OR of trace flags:
	0x0001 mwavedd api tracing
	0x0002 smapi api tracing
	0x0004 3780i tracing
	0x0008 tp3780i tracing

        Tracing only occurs if the driver has been compiled with the
        MW_TRACE macro #defined  (i.e. let EXTRA_CFLAGS += -DMW_TRACE
        in the Makefile).

  mwave_3780i_irq=5/7/10/11/15
	If the dsp irq has not been setup and stored in bios by the 
	thinkpad configuration utility then this parameter allows the
	irq used by the dsp to be configured.

  mwave_3780i_io=0x130/0x350/0x0070/0xDB0
	If the dsp io range has not been setup and stored in bios by the 
	thinkpad configuration utility then this parameter allows the
	io range used by the dsp to be configured.

  mwave_uart_irq=3/4
	If the mwave's uart irq has not been setup and stored in bios by the 
	thinkpad configuration utility then this parameter allows the
	irq used by the mwave uart to be configured.

  mwave_uart_io=0x3f8/0x2f8/0x3E8/0x2E8
	If the uart io range has not been setup and stored in bios by the 
	thinkpad configuration utility then this parameter allows the
	io range used by the mwave uart to be configured.

Example to enable the 3780i DSP using ttyS1 resources:
	
  insmod mwave mwave_3780i_irq=10 mwave_3780i_io=0x0130 mwave_uart_irq=3 mwave_uart_io=0x2f8

Accessing the driver
--------------------

You must also create a node for the driver:
  mkdir -p /dev/modems
  mknod --mode=660 /dev/modems/mwave c 10 219

