#!/usr/bin/env python

"""Simple harness that benchmarks different variants of the routines,
caches the results, and emits all of the records at the end.

Results are generated for different values of:
 * Source
 * Routine
 * Length
 * Alignment
"""

import argparse
import subprocess
import math
import sys

# Prefix to the executables
build = '../build/try-'

ALL = 'memchr memcmp memcpy memset strchr strcmp strcpy strlen'

HAS = {
    'this': 'bounce memchr memcpy memset strchr strcmp strcpy strlen',
    'bionic-a9': 'memcmp memcpy memset strcmp strcpy strlen',
    'bionic-a15': 'memcmp memcpy memset strcmp strcpy strlen',
    'bionic-c': ALL,
    'csl': 'memcpy memset',
    'glibc': 'memcpy memset strchr strlen',
    'glibc-c': ALL,
    'newlib': 'memcpy strcmp strcpy strlen',
    'newlib-c': ALL,
    'newlib-xscale': 'memchr memcpy memset strchr strcmp strcpy strlen',
    'plain': 'memset memcpy strcmp strcpy',
}

BOUNCE_ALIGNMENTS = ['1']
SINGLE_BUFFER_ALIGNMENTS = ['1', '2', '4', '8', '16', '32']
DUAL_BUFFER_ALIGNMENTS = ['1:32', '2:32', '4:32', '8:32', '16:32', '32:32']

ALIGNMENTS = {
    'bounce': BOUNCE_ALIGNMENTS,
    'memchr': SINGLE_BUFFER_ALIGNMENTS,
    'memset': SINGLE_BUFFER_ALIGNMENTS,
    'strchr': SINGLE_BUFFER_ALIGNMENTS,
    'strlen': SINGLE_BUFFER_ALIGNMENTS,
    'memcmp': DUAL_BUFFER_ALIGNMENTS,
    'memcpy': DUAL_BUFFER_ALIGNMENTS,
    'strcmp': DUAL_BUFFER_ALIGNMENTS,
    'strcpy': DUAL_BUFFER_ALIGNMENTS,
}

VARIANTS = sorted(HAS.keys())
FUNCTIONS = sorted(ALIGNMENTS.keys())

NUM_RUNS = 5

def run(cache, variant, function, bytes, loops, alignment, run_id, quiet=False):
    """Perform a single run, exercising the cache as appropriate."""
    key = ':'.join('%s' % x for x in (variant, function, bytes, loops, alignment, run_id))

    if key in cache:
        got = cache[key]
    else:
        xbuild = build
        cmd = '%(xbuild)s%(variant)s -t %(function)s -c %(bytes)s -l %(loops)s -a %(alignment)s -r %(run_id)s' % locals()

        try:
            got = subprocess.check_output(cmd.split()).strip()
        except OSError, ex:
            assert False, 'Error %s while running %s' % (ex, cmd)

    parts = got.split(':')
    took = float(parts[7])

    cache[key] = got

    if not quiet:
        print got
        sys.stdout.flush()

    return took

def run_many(cache, variants, bytes, all_functions):
    # We want the data to come out in a useful order.  So fix an
    # alignment and function, and do all sizes for a variant first
    bytes = sorted(bytes)
    mid = bytes[int(len(bytes)/1.5)]

    if not all_functions:
        # Use the ordering in 'this' as the default
        all_functions = HAS['this'].split()

        # Find all other functions
        for functions in HAS.values():
            for function in functions.split():
                if function not in all_functions:
                    all_functions.append(function)

    for function in all_functions:
        for alignment in ALIGNMENTS[function]:
            for variant in variants:
                if function not in HAS[variant].split():
                    continue

                # Run a tracer through and see how long it takes and
                # adjust the number of loops based on that.  Not great
                # for memchr() and similar which are O(n), but it will
                # do
                f = 50000000
                want = 5.0

                loops = int(f / math.sqrt(max(1, mid)))
                took = run(cache, variant, function, mid, loops, alignment, 0,
                           quiet=True)
                # Keep it reasonable for silly routines like bounce
                factor = min(20, max(0.05, want/took))
                f = f * factor
                
                # Round f to a few significant figures
                scale = 10**int(math.log10(f) - 1)
                f = scale*int(f/scale)

                for b in sorted(bytes):
                    # Figure out the number of loops to give a roughly consistent run
                    loops = int(f / math.sqrt(max(1, b)))
                    for run_id in range(0, NUM_RUNS):
                        run(cache, variant, function, b, loops, alignment,
                            run_id)

def run_top(cache):
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--variants", nargs="+", help="library variant to run (run all if not specified)", default = VARIANTS, choices = VARIANTS)
    parser.add_argument("-f", "--functions", nargs="+", help="function to run (run all if not specified)", default = FUNCTIONS, choices = FUNCTIONS)
    parser.add_argument("-l", "--limit", type=int, help="upper limit to test to (in bytes)", default = 512*1024)
    args = parser.parse_args()

    # Test all powers of 2
    step1 = 2.0
    # Test intermediate powers of 1.4
    step2 = 1.4
    
    bytes = []
    
    for step in [step1, step2]:
        if step:
            # Figure out how many steps get us up to the top
            steps = int(round(math.log(args.limit) / math.log(step)))
            bytes.extend([int(step**x) for x in range(0, steps+1)])

    run_many(cache, args.variants, bytes, args.functions)

def main():
    cachename = 'cache.txt'

    cache = {}

    try:
        with open(cachename) as f:
            for line in f:
                line = line.strip()
                parts = line.split(':')
                cache[':'.join(parts[:7])] = line
    except:
        pass

    try:
        run_top(cache)
    finally:
        with open(cachename, 'w') as f:
            for line in sorted(cache.values()):
                print >> f, line

if __name__ == '__main__':
    main()
