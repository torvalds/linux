# libdivsufsort

libdivsufsort is a software library that implements a lightweight suffix array construction algorithm.

## News
* 2015-03-21: The project has moved from [Google Code](http://code.google.com/p/libdivsufsort/) to [GitHub](https://github.com/y-256/libdivsufsort)

## Introduction
This library provides a simple and an efficient C API to construct a suffix array and a Burrows-Wheeler transformed string from a given string over a constant-size alphabet.
The algorithm runs in O(n log n) worst-case time using only 5n+O(1) bytes of memory space, where n is the length of
the string.

## Build requirements
* An ANSI C Compiler (e.g. GNU GCC)
* [CMake](http://www.cmake.org/ "CMake") version 2.4.2 or newer
* CMake-supported build tool

## Building on GNU/Linux
1. Get the source code from GitHub. You can either
    * use git to clone the repository
    ```
    git clone https://github.com/y-256/libdivsufsort.git
    ```
    * or download a [zip file](../../archive/master.zip) directly
2. Create a `build` directory in the package source directory.
```shell
$ cd libdivsufsort
$ mkdir build
$ cd build
```
3. Configure the package for your system.
If you want to install to a different location,  change the -DCMAKE_INSTALL_PREFIX option.
```shell
$ cmake -DCMAKE_BUILD_TYPE="Release" \
-DCMAKE_INSTALL_PREFIX="/usr/local" ..
```
4. Compile the package.
```shell
$ make
```
5. (Optional) Install the library and header files.
```shell
$ sudo make install
```

## API
```c
/* Data types */
typedef int32_t saint_t;
typedef int32_t saidx_t;
typedef uint8_t sauchar_t;

/*
 * Constructs the suffix array of a given string.
 * @param T[0..n-1] The input string.
 * @param SA[0..n-1] The output array or suffixes.
 * @param n The length of the given string.
 * @return 0 if no error occurred, -1 or -2 otherwise.
 */
saint_t
divsufsort(const sauchar_t *T, saidx_t *SA, saidx_t n);

/*
 * Constructs the burrows-wheeler transformed string of a given string.
 * @param T[0..n-1] The input string.
 * @param U[0..n-1] The output string. (can be T)
 * @param A[0..n-1] The temporary array. (can be NULL)
 * @param n The length of the given string.
 * @return The primary index if no error occurred, -1 or -2 otherwise.
 */
saidx_t
divbwt(const sauchar_t *T, sauchar_t *U, saidx_t *A, saidx_t n);
```

## Example Usage
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <divsufsort.h>

int main() {
    // intput data
    char *Text = "abracadabra";
    int n = strlen(Text);
    int i, j;

    // allocate
    int *SA = (int *)malloc(n * sizeof(int));

    // sort
    divsufsort((unsigned char *)Text, SA, n);

    // output
    for(i = 0; i < n; ++i) {
        printf("SA[%2d] = %2d: ", i, SA[i]);
        for(j = SA[i]; j < n; ++j) {
            printf("%c", Text[j]);
        }
        printf("$\n");
    }

    // deallocate
    free(SA);

    return 0;
}
```
See the [examples](examples) directory for a few other examples.

## Benchmarks
See [Benchmarks](https://github.com/y-256/libdivsufsort/blob/wiki/SACA_Benchmarks.md) page for details.

## License
libdivsufsort is released under the [MIT license](LICENSE "MIT license").
> The MIT License (MIT)
>
> Copyright (c) 2003 Yuta Mori All rights reserved.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## Author
* Yuta Mori
