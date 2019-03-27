#!/bin/bash -eu
#
# Run as: CLANG=bin/clang ZLIB_SRC=src/zlib \
#             build_symbolizer.sh runtime_build/lib/clang/4.0.0/lib/linux/
# zlib can be downloaded from http://www.zlib.net.
#
# Script compiles self-contained object file with symbolization code and injects
# it into the given set of runtime libraries. Script updates only libraries
# which has unresolved __sanitizer_symbolize_* symbols and matches architecture.
# Object file is be compiled from LLVM sources with dependencies like libc++ and
# zlib. Then it internalizes symbols in the file, so that it can be linked
# into arbitrary programs, avoiding conflicts with the program own symbols and
# avoiding dependencies on any program symbols. The only acceptable dependencies
# are libc and __sanitizer::internal_* from sanitizer runtime.
#
# Symbols exported by the object file will be used by Sanitizer runtime
# libraries to symbolize code/data in-process.
#
# The script will modify the output directory which is given as the first
# argument to the script.
#
# FIXME: We should really be using a simpler approach to building this object
# file, and it should be available as a regular cmake rule. Conceptually, we
# want to be doing "ld -r" followed by "objcopy -G" to create a relocatable
# object file with only our entry points exposed. However, this does not work at
# present, see PR30750.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR=$(readlink -f $SCRIPT_DIR/..)
TARGE_DIR=$(readlink -f $1)

LLVM_SRC="${LLVM_SRC:-$SCRIPT_DIR/../../../../../..}"
LLVM_SRC=$(readlink -f $LLVM_SRC)

if [[ ! -d "${LLVM_SRC}/projects/libcxxabi" ||
      ! -d "${LLVM_SRC}/projects/libcxx" ]]; then
  echo "Missing or incomplete LLVM_SRC"
  exit 1
fi

if [[ "$ZLIB_SRC" == ""  ||
      ! -x "${ZLIB_SRC}/configure" ||
      ! -f "${ZLIB_SRC}/zlib.h" ]]; then
  echo "Missing or incomplete ZLIB_SRC"
  exit 1
fi
ZLIB_SRC=$(readlink -f $ZLIB_SRC)

J="${J:-50}"

CLANG="${CLANG:-`which clang`}"
CLANG_DIR=$(readlink -f $(dirname "$CLANG"))

BUILD_DIR=$(readlink -f ./symbolizer)
mkdir -p $BUILD_DIR
cd $BUILD_DIR

CC=$CLANG_DIR/clang
CXX=$CLANG_DIR/clang++
TBLGEN=$CLANG_DIR/llvm-tblgen
OPT=$CLANG_DIR/opt
export AR=$CLANG_DIR/llvm-ar
export LINK=$CLANG_DIR/llvm-link

for F in $CC $CXX $TBLGEN $LINK $OPT $AR; do
  if [[ ! -x "$F" ]]; then
    echo "Missing $F"
     exit 1
  fi
done

ZLIB_BUILD=${BUILD_DIR}/zlib
LIBCXX_BUILD=${BUILD_DIR}/libcxx
LLVM_BUILD=${BUILD_DIR}/llvm
SYMBOLIZER_BUILD=${BUILD_DIR}/symbolizer

FLAGS=${FLAGS:-}
FLAGS="$FLAGS -fPIC -flto -Os -g0 -DNDEBUG"

# Build zlib.
mkdir -p ${ZLIB_BUILD}
cd ${ZLIB_BUILD}
cp -r ${ZLIB_SRC}/* .
CC=$CC CFLAGS="$FLAGS" RANLIB=/bin/true ./configure --static
make -j${J} libz.a

# Build and install libcxxabi and libcxx.
if [[ ! -d ${LIBCXX_BUILD} ]]; then
  mkdir -p ${LIBCXX_BUILD}
  cd ${LIBCXX_BUILD}
  LIBCXX_FLAGS="${FLAGS} -Wno-macro-redefined -I${LLVM_SRC}/projects/libcxxabi/include"
  cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX \
    -DCMAKE_C_FLAGS_RELEASE="${LIBCXX_FLAGS}" \
    -DCMAKE_CXX_FLAGS_RELEASE="${LIBCXX_FLAGS}" \
    -DLIBCXXABI_ENABLE_ASSERTIONS=OFF \
    -DLIBCXXABI_ENABLE_EXCEPTIONS=OFF \
    -DLIBCXXABI_ENABLE_SHARED=OFF \
    -DLIBCXX_ENABLE_ASSERTIONS=OFF \
    -DLIBCXX_ENABLE_EXCEPTIONS=OFF \
    -DLIBCXX_ENABLE_RTTI=OFF \
    -DLIBCXX_ENABLE_SHARED=OFF \
  $LLVM_SRC
fi
cd ${LIBCXX_BUILD}
ninja cxx cxxabi

FLAGS="${FLAGS} -fno-rtti -fno-exceptions"
LLVM_FLAGS="${FLAGS} -nostdinc++ -I${ZLIB_BUILD} -I${LIBCXX_BUILD}/include/c++/v1"

# Build LLVM.
if [[ ! -d ${LLVM_BUILD} ]]; then
  mkdir -p ${LLVM_BUILD}
  cd ${LLVM_BUILD}
  cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX \
    -DCMAKE_C_FLAGS_RELEASE="${LLVM_FLAGS}" \
    -DCMAKE_CXX_FLAGS_RELEASE="${LLVM_FLAGS}" \
    -DLLVM_TABLEGEN=$TBLGEN \
    -DLLVM_ENABLE_ZLIB=ON \
    -DLLVM_ENABLE_TERMINFO=OFF \
    -DLLVM_ENABLE_THREADS=OFF \
  $LLVM_SRC
fi
cd ${LLVM_BUILD}
ninja LLVMSymbolize LLVMObject LLVMBinaryFormat LLVMDebugInfoDWARF LLVMSupport LLVMDebugInfoPDB LLVMMC LLVMDemangle

cd ${BUILD_DIR}
rm -rf ${SYMBOLIZER_BUILD}
mkdir ${SYMBOLIZER_BUILD}
cd ${SYMBOLIZER_BUILD}

echo "Compiling..."
SYMBOLIZER_FLAGS="$LLVM_FLAGS -I${LLVM_SRC}/include -I${LLVM_BUILD}/include -std=c++11"
$CXX $SYMBOLIZER_FLAGS ${SRC_DIR}/sanitizer_symbolize.cc ${SRC_DIR}/sanitizer_wrappers.cc -c
$AR rc symbolizer.a sanitizer_symbolize.o sanitizer_wrappers.o

SYMBOLIZER_API_LIST=__sanitizer_symbolize_code,__sanitizer_symbolize_data,__sanitizer_symbolize_flush,__sanitizer_symbolize_demangle

# Merge all the object files together and copy the resulting library back.
$SCRIPT_DIR/ar_to_bc.sh $LIBCXX_BUILD/lib/libc++.a \
                        $LIBCXX_BUILD/lib/libc++abi.a \
                        $LLVM_BUILD/lib/libLLVMSymbolize.a \
                        $LLVM_BUILD/lib/libLLVMObject.a \
                        $LLVM_BUILD/lib/libLLVMBinaryFormat.a \
                        $LLVM_BUILD/lib/libLLVMDebugInfoDWARF.a \
                        $LLVM_BUILD/lib/libLLVMSupport.a \
                        $LLVM_BUILD/lib/libLLVMDebugInfoPDB.a \
                        $LLVM_BUILD/lib/libLLVMDemangle.a \
                        $LLVM_BUILD/lib/libLLVMMC.a \
                        $ZLIB_BUILD/libz.a \
                        symbolizer.a \
                        all.bc

echo "Optimizing..."
$OPT -internalize -internalize-public-api-list=${SYMBOLIZER_API_LIST} all.bc -o opt.bc
$CC $FLAGS -fno-lto -c opt.bc -o symbolizer.o

echo "Checking undefined symbols..."
nm -f posix -g symbolizer.o | cut -f 1,2 -d \  | LC_COLLATE=C sort -u > undefined.new
(diff -u $SCRIPT_DIR/global_symbols.txt undefined.new | grep -E "^\+[^+]") && \
  (echo "Failed: unexpected symbols"; exit 1)

arch() {
  objdump -f $1 | grep -m1 -Po "(?<=file format ).*$"
}

SYMBOLIZER_FORMAT=$(arch symbolizer.o)
echo "Injecting $SYMBOLIZER_FORMAT symbolizer..."
for A in $TARGE_DIR/libclang_rt.*san*.a; do
  A_FORMAT=$(arch $A)
  if [[ "$A_FORMAT" != "$SYMBOLIZER_FORMAT" ]] ; then
    continue
  fi
  (nm -u $A 2>/dev/null | grep -E "__sanitizer_symbolize_code" >/dev/null) || continue
  echo "$A"
  $AR rcs $A symbolizer.o
done

echo "Success!"
