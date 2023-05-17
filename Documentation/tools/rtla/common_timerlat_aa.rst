**--dump-tasks**

        prints the task running on all CPUs if stop conditions are met (depends on !--no-aa)

**--no-aa**

        disable auto-analysis, reducing rtla timerlat cpu usage

**--aa-only** *us*

        Set stop tracing conditions and run without collecting and displaying statistics.
        Print the auto-analysis if the system hits the stop tracing condition. This option
        is useful to reduce rtla timerlat CPU, enabling the debug without the overhead of
        collecting the statistics.
