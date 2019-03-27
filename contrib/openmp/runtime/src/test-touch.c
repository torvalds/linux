// test-touch.c //


//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


#ifdef __cplusplus
extern "C" {
#endif
extern double omp_get_wtime();
extern int    omp_get_num_threads();
extern int    omp_get_max_threads();
#ifdef __cplusplus
}
#endif

int main() {
    omp_get_wtime();
    omp_get_num_threads();
    omp_get_max_threads();
    return 0;
}

// end of file //
