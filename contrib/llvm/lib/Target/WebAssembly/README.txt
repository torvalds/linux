//===-- README.txt - Notes for WebAssembly code gen -----------------------===//

This WebAssembly backend is presently under development.

The most notable feature which is not yet stable is the ".o" file format.
".o" file support is needed for many common ways of using LLVM, such as
using it through "clang -c", so this backend is not yet considered widely
usable. However, this backend is usable within some language toolchain
packages:

Emscripten provides a C/C++ compilation environment that includes standard
libraries, tools, and packaging for producing WebAssembly applications that
can run in browsers and other environments. For more information, see the
Emscripten documentation in general, and this page in particular:

  * https://github.com/kripken/emscripten/wiki/New-WebAssembly-Backend
 
Rust provides WebAssembly support integrated into Cargo. There are two
main options:
 - wasm32-unknown-unknown, which provides a relatively minimal environment
   that has an emphasis on being "native"
 - wasm32-unknown-emscripten, which uses Emscripten internally and
   provides standard C/C++ libraries, filesystem emulation, GL and SDL
   bindings
For more information, see:
  * https://www.hellorust.com/


This backend does not yet support debug info. Full DWARF support needs a
design for how DWARF should be represented in WebAssembly. Sourcemap support
has an existing design and some corresponding browser implementations, so it
just needs implementing in LLVM.

Work-in-progress documentation for the ".o" file format is here:

  * https://github.com/WebAssembly/tool-conventions/blob/master/Linking.md

A corresponding linker implementation is also under development:

  * https://lld.llvm.org/WebAssembly.html

For more information on WebAssembly itself, see the home page:
  * https://webassembly.github.io/

The following documents contain some information on the semantics and binary
encoding of WebAssembly itself:
  * https://github.com/WebAssembly/design/blob/master/Semantics.md
  * https://github.com/WebAssembly/design/blob/master/BinaryEncoding.md

The backend is built, tested and archived on the following waterfall:
  https://wasm-stat.us

The backend's bringup is done in part by using the GCC torture test suite, since
it doesn't require C library support. Current known failures are in
known_gcc_test_failures.txt, all other tests should pass. The waterfall will
turn red if not. Once most of these pass, further testing will use LLVM's own
test suite. The tests can be run locally using:
  https://github.com/WebAssembly/waterfall/blob/master/src/compile_torture_tests.py

Some notes on ways that the generated code could be improved follow:

//===---------------------------------------------------------------------===//

Br, br_if, and br_table instructions can support having a value on the value
stack across the jump (sometimes). We should (a) model this, and (b) extend
the stackifier to utilize it.

//===---------------------------------------------------------------------===//

The min/max instructions aren't exactly a<b?a:b because of NaN and negative zero
behavior. The ARM target has the same kind of min/max instructions and has
implemented optimizations for them; we should do similar optimizations for
WebAssembly.

//===---------------------------------------------------------------------===//

AArch64 runs SeparateConstOffsetFromGEPPass, followed by EarlyCSE and LICM.
Would these be useful to run for WebAssembly too? Also, it has an option to
run SimplifyCFG after running the AtomicExpand pass. Would this be useful for
us too?

//===---------------------------------------------------------------------===//

Register stackification uses the VALUE_STACK physical register to impose
ordering dependencies on instructions with stack operands. This is pessimistic;
we should consider alternate ways to model stack dependencies.

//===---------------------------------------------------------------------===//

Lots of things could be done in WebAssemblyTargetTransformInfo.cpp. Similarly,
there are numerous optimization-related hooks that can be overridden in
WebAssemblyTargetLowering.

//===---------------------------------------------------------------------===//

Instead of the OptimizeReturned pass, which should consider preserving the
"returned" attribute through to MachineInstrs and extending the
MemIntrinsicResults pass to do this optimization on calls too. That would also
let the WebAssemblyPeephole pass clean up dead defs for such calls, as it does
for stores.

//===---------------------------------------------------------------------===//

Consider implementing optimizeSelect, optimizeCompareInstr, optimizeCondBranch,
optimizeLoadInstr, and/or getMachineCombinerPatterns.

//===---------------------------------------------------------------------===//

Find a clean way to fix the problem which leads to the Shrink Wrapping pass
being run after the WebAssembly PEI pass.

//===---------------------------------------------------------------------===//

When setting multiple local variables to the same constant, we currently get
code like this:

    i32.const   $4=, 0
    i32.const   $3=, 0

It could be done with a smaller encoding like this:

    i32.const   $push5=, 0
    local.tee   $push6=, $4=, $pop5
    local.copy  $3=, $pop6

//===---------------------------------------------------------------------===//

WebAssembly registers are implicitly initialized to zero. Explicit zeroing is
therefore often redundant and could be optimized away.

//===---------------------------------------------------------------------===//

Small indices may use smaller encodings than large indices.
WebAssemblyRegColoring and/or WebAssemblyRegRenumbering should sort registers
according to their usage frequency to maximize the usage of smaller encodings.

//===---------------------------------------------------------------------===//

Many cases of irreducible control flow could be transformed more optimally
than via the transform in WebAssemblyFixIrreducibleControlFlow.cpp.

It may also be worthwhile to do transforms before register coloring,
particularly when duplicating code, to allow register coloring to be aware of
the duplication.

//===---------------------------------------------------------------------===//

WebAssemblyRegStackify could use AliasAnalysis to reorder loads and stores more
aggressively.

//===---------------------------------------------------------------------===//

WebAssemblyRegStackify is currently a greedy algorithm. This means that, for
example, a binary operator will stackify with its user before its operands.
However, if moving the binary operator to its user moves it to a place where
its operands can't be moved to, it would be better to leave it in place, or
perhaps move it up, so that it can stackify its operands. A binary operator
has two operands and one result, so in such cases there could be a net win by
preferring the operands.

//===---------------------------------------------------------------------===//

Instruction ordering has a significant influence on register stackification and
coloring. Consider experimenting with the MachineScheduler (enable via
enableMachineScheduler) and determine if it can be configured to schedule
instructions advantageously for this purpose.

//===---------------------------------------------------------------------===//

WebAssemblyRegStackify currently assumes that the stack must be empty after
an instruction with no return values, however wasm doesn't actually require
this. WebAssemblyRegStackify could be extended, or possibly rewritten, to take
full advantage of what WebAssembly permits.

//===---------------------------------------------------------------------===//

Add support for mergeable sections in the Wasm writer, such as for strings and
floating-point constants.

//===---------------------------------------------------------------------===//

The function @dynamic_alloca_redzone in test/CodeGen/WebAssembly/userstack.ll
ends up with a local.tee in its prolog which has an unused result, requiring
an extra drop:

    global.get  $push8=, 0
    local.tee   $push9=, 1, $pop8
    drop        $pop9
    [...]

The prologue code initially thinks it needs an FP register, but later it
turns out to be unneeded, so one could either approach this by being more
clever about not inserting code for an FP in the first place, or optimizing
away the copy later.

//===---------------------------------------------------------------------===//
