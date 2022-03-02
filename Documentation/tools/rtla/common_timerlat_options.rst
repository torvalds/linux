**-a**, **--auto** *us*

        Set the automatic trace mode. This mode sets some commonly used options
        while debugging the system. It is equivalent to use **-T** *us* **-s** *us*
        **-t**. By default, *timerlat* tracer uses FIFO:95 for *timerlat* threads,
        thus equilavent to **-P** *f:95*.

**-p**, **--period** *us*

        Set the *timerlat* tracer period in microseconds.

**-i**, **--irq** *us*

        Stop trace if the *IRQ* latency is higher than the argument in us.

**-T**, **--thread** *us*

        Stop trace if the *Thread* latency is higher than the argument in us.

**-s**, **--stack** *us*

        Save the stack trace at the *IRQ* if a *Thread* latency is higher than the
        argument in us.
