**-a**, **--auto** *us*

        Set the automatic trace mode. This mode sets some commonly used options
        while debugging the system. It is equivalent to use **-s** *us* **-T 1 -t**.

**-p**, **--period** *us*

        Set the *osnoise* tracer period in microseconds.

**-r**, **--runtime** *us*

        Set the *osnoise* tracer runtime in microseconds.

**-s**, **--stop** *us*

        Stop the trace if a single sample is higher than the argument in microseconds.
        If **-T** is set, it will also save the trace to the output.

**-S**, **--stop-total** *us*

        Stop the trace if the total sample is higher than the argument in microseconds.
        If **-T** is set, it will also save the trace to the output.

**-T**, **--threshold** *us*

        Specify the minimum delta between two time reads to be considered noise.
        The default threshold is *5 us*.

**-t**, **--trace** \[*file*]

        Save the stopped trace to [*file|osnoise_trace.txt*].
