# ktap

A New Scripting Dynamic Tracing Tool For Linux  
[www.ktap.org][homepage]

ktap is a new scripting dynamic tracing tool for Linux,
it uses a scripting language and lets users trace the Linux kernel dynamically.
ktap is designed to give operational insights with interoperability
that allows users to tune, troubleshoot and extend kernel and application.
It's similar with Linux Systemtap and Solaris Dtrace.

ktap have different design principles from Linux mainstream dynamic tracing
language in that it's based on bytecode, so it doesn't depend upon GCC,
doesn't require compiling kernel module for each script, safe to use in
production environment, fulfilling the embedded ecosystem's tracing needs.

More information can be found at [ktap homepage][homepage].

[homepage]: http://www.ktap.org

## Highlights

  * simple but powerful scripting language
  * register based interpreter (heavily optimized) in Linux kernel
  * small and lightweight (6KLOC of interpreter)
  * not depend on gcc for each script running
  * easy to use in embedded environment without debugging info
  * support for tracepoint, kprobe, uprobe, function trace, timer, and more
  * supported in x86, arm, ppc, mips
  * safety in sandbox

## Building & Running

1. Clone ktap from github

        $ git clone http://github.com/ktap/ktap.git

2. Compiling ktap

        $ cd ktap
        $ make       #generate ktapvm kernel module and ktap binary

3. Load ktapvm kernel module(make sure debugfs mounted)

        $ make load  #need to be root or have sudo access

4. Running ktap

        $ ./ktap scripts/helloworld.kp


## Examples

1. simplest one-liner command to enable all tracepoints

        ktap -e "trace *:* { print(argevent) }"

2. syscall tracing on target process

        ktap -e "trace syscalls:* { print(argevent) }" -- ls

3. function tracing

        ktap -e "trace ftrace:function { print(argevent) }"

        ktap -e "trace ftrace:function /ip==mutex*/ { print(argevent) }"

4. simple syscall tracing

        trace syscalls:* {
                print(cpu(), pid(), execname(), argevent)
        }

5. syscall tracing in histogram style

        s = {}

        trace syscalls:sys_enter_* {
                s[argname] += 1
        }

        trace_end {
                histogram(s)
        }

6. kprobe tracing

        trace probe:do_sys_open dfd=%di fname=%dx flags=%cx mode=+4($stack) {
                print("entry:", execname(), argevent)
        }

        trace probe:do_sys_open%return fd=$retval {
                print("exit:", execname(), argevent)
        }

7. uprobe tracing

        trace probe:/lib/libc.so.6:0x000773c0 {
                print("entry:", execname(), argevent)
        }

        trace probe:/lib/libc.so.6:0x000773c0%return {
                print("exit:", execname(), argevent)
        }

8. timer

        tick-1ms {
                printf("time fired on one cpu\n");
        }

        profile-2s {
                printf("time fired on every cpu\n");
        }

More sample scripts can be found at scripts/ directory.

## Mailing list

ktap@freelists.org  
You can subscribe to ktap mailing list at link (subscribe before posting):
http://www.freelists.org/list/ktap


## Copyright and License

ktap is licensed under GPL v2

Copyright (C) 2012-2013, Jovi Zhangwei <jovi.zhangwei@gmail.com>.
All rights reserved.  


## Contribution

ktap is still under active development, so contributions are welcome.
You are encouraged to report bugs, provide feedback, send feature request,
or hack on it.


## See More

More info can be found at [documentation][tutorial]
[tutorial]: http://www.ktap.org/doc/tutorial.html

