BPF extensibility and applicability to networking, tracing, security
in the linux kernel and several user space implementations of BPF
virtual machine led to a number of misunderstanding on what BPF actually is.
This short QA is an attempt to address that and outline a direction
of where BPF is heading long term.

Q: Is BPF a generic instruction set similar to x64 and arm64?
A: NO.

Q: Is BPF a generic virtual machine ?
A: NO.

BPF is generic instruction set _with_ C calling convention.

Q: Why C calling convention was chosen?
A: Because BPF programs are designed to run in the linux kernel
   which is written in C, hence BPF defines instruction set compatible
   with two most used architectures x64 and arm64 (and takes into
   consideration important quirks of other architectures) and
   defines calling convention that is compatible with C calling
   convention of the linux kernel on those architectures.

Q: can multiple return values be supported in the future?
A: NO. BPF allows only register R0 to be used as return value.

Q: can more than 5 function arguments be supported in the future?
A: NO. BPF calling convention only allows registers R1-R5 to be used
   as arguments. BPF is not a standalone instruction set.
   (unlike x64 ISA that allows msft, cdecl and other conventions)

Q: can BPF programs access instruction pointer or return address?
A: NO.

Q: can BPF programs access stack pointer ?
A: NO. Only frame pointer (register R10) is accessible.
   From compiler point of view it's necessary to have stack pointer.
   For example LLVM defines register R11 as stack pointer in its
   BPF backend, but it makes sure that generated code never uses it.

Q: Does C-calling convention diminishes possible use cases?
A: YES. BPF design forces addition of major functionality in the form
   of kernel helper functions and kernel objects like BPF maps with
   seamless interoperability between them. It lets kernel call into
   BPF programs and programs call kernel helpers with zero overhead.
   As all of them were native C code. That is particularly the case
   for JITed BPF programs that are indistinguishable from
   native kernel C code.

Q: Does it mean that 'innovative' extensions to BPF code are disallowed?
A: Soft yes. At least for now until BPF core has support for
   bpf-to-bpf calls, indirect calls, loops, global variables,
   jump tables, read only sections and all other normal constructs
   that C code can produce.

Q: Can loops be supported in a safe way?
A: It's not clear yet. BPF developers are trying to find a way to
   support bounded loops where the verifier can guarantee that
   the program terminates in less than 4096 instructions.

Q: How come LD_ABS and LD_IND instruction are present in BPF whereas
   C code cannot express them and has to use builtin intrinsics?
A: This is artifact of compatibility with classic BPF. Modern
   networking code in BPF performs better without them.
   See 'direct packet access'.

Q: It seems not all BPF instructions are one-to-one to native CPU.
   For example why BPF_JNE and other compare and jumps are not cpu-like?
A: This was necessary to avoid introducing flags into ISA which are
   impossible to make generic and efficient across CPU architectures.

Q: why BPF_DIV instruction doesn't map to x64 div?
A: Because if we picked one-to-one relationship to x64 it would have made
   it more complicated to support on arm64 and other archs. Also it
   needs div-by-zero runtime check.

Q: why there is no BPF_SDIV for signed divide operation?
A: Because it would be rarely used. llvm errors in such case and
   prints a suggestion to use unsigned divide instead

Q: Why BPF has implicit prologue and epilogue?
A: Because architectures like sparc have register windows and in general
   there are enough subtle differences between architectures, so naive
   store return address into stack won't work. Another reason is BPF has
   to be safe from division by zero (and legacy exception path
   of LD_ABS insn). Those instructions need to invoke epilogue and
   return implicitly.

Q: Why BPF_JLT and BPF_JLE instructions were not introduced in the beginning?
A: Because classic BPF didn't have them and BPF authors felt that compiler
   workaround would be acceptable. Turned out that programs lose performance
   due to lack of these compare instructions and they were added.
   These two instructions is a perfect example what kind of new BPF
   instructions are acceptable and can be added in the future.
   These two already had equivalent instructions in native CPUs.
   New instructions that don't have one-to-one mapping to HW instructions
   will not be accepted.

Q: BPF 32-bit subregisters have a requirement to zero upper 32-bits of BPF
   registers which makes BPF inefficient virtual machine for 32-bit
   CPU architectures and 32-bit HW accelerators. Can true 32-bit registers
   be added to BPF in the future?
A: NO. The first thing to improve performance on 32-bit archs is to teach
   LLVM to generate code that uses 32-bit subregisters. Then second step
   is to teach verifier to mark operations where zero-ing upper bits
   is unnecessary. Then JITs can take advantage of those markings and
   drastically reduce size of generated code and improve performance.

Q: Does BPF have a stable ABI?
A: YES. BPF instructions, arguments to BPF programs, set of helper
   functions and their arguments, recognized return codes are all part
   of ABI. However when tracing programs are using bpf_probe_read() helper
   to walk kernel internal datastructures and compile with kernel
   internal headers these accesses can and will break with newer
   kernels. The union bpf_attr -> kern_version is checked at load time
   to prevent accidentally loading kprobe-based bpf programs written
   for a different kernel. Networking programs don't do kern_version check.

Q: How much stack space a BPF program uses?
A: Currently all program types are limited to 512 bytes of stack
   space, but the verifier computes the actual amount of stack used
   and both interpreter and most JITed code consume necessary amount.

Q: Can BPF be offloaded to HW?
A: YES. BPF HW offload is supported by NFP driver.

Q: Does classic BPF interpreter still exist?
A: NO. Classic BPF programs are converted into extend BPF instructions.

Q: Can BPF call arbitrary kernel functions?
A: NO. BPF programs can only call a set of helper functions which
   is defined for every program type.

Q: Can BPF overwrite arbitrary kernel memory?
A: NO. Tracing bpf programs can _read_ arbitrary memory with bpf_probe_read()
   and bpf_probe_read_str() helpers. Networking programs cannot read
   arbitrary memory, since they don't have access to these helpers.
   Programs can never read or write arbitrary memory directly.

Q: Can BPF overwrite arbitrary user memory?
A: Sort-of. Tracing BPF programs can overwrite the user memory
   of the current task with bpf_probe_write_user(). Every time such
   program is loaded the kernel will print warning message, so
   this helper is only useful for experiments and prototypes.
   Tracing BPF programs are root only.

Q: When bpf_trace_printk() helper is used the kernel prints nasty
   warning message. Why is that?
A: This is done to nudge program authors into better interfaces when
   programs need to pass data to user space. Like bpf_perf_event_output()
   can be used to efficiently stream data via perf ring buffer.
   BPF maps can be used for asynchronous data sharing between kernel
   and user space. bpf_trace_printk() should only be used for debugging.

Q: Can BPF functionality such as new program or map types, new
   helpers, etc be added out of kernel module code?
A: NO.
