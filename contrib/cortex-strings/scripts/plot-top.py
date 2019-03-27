#!/usr/bin/env python

"""Plot the performance of different variants of the string routines
for one size.
"""

import libplot

import pylab


def plot(records, bytes):
    records = [x for x in records if x.bytes==bytes]

    variants = libplot.unique(records, 'variant', prefer='this')
    functions = libplot.unique(records, 'function')

    X = pylab.arange(len(functions))
    width = 1.0/(len(variants)+1)

    colours = libplot.make_colours()

    pylab.figure(1).set_size_inches((16, 12))
    pylab.clf()

    for i, variant in enumerate(variants):
        heights = []

        for function in functions:
            matches = [x for x in records if x.variant==variant and x.function==function and x.src_alignment==8]

            if matches:
                vals = [match.bytes*match.loops/match.elapsed/(1024*1024) for
                        match in matches]
                mean = sum(vals)/len(vals)
                heights.append(mean)
            else:
                heights.append(0)

        pylab.bar(X+i*width, heights, width, color=colours.next(), label=variant)

    axes = pylab.axes()
    axes.set_xticklabels(functions)
    axes.set_xticks(X + 0.5)

    pylab.title('Performance of different variants for %d byte blocks' % bytes)
    pylab.ylabel('Rate (MB/s)')
    pylab.legend(loc='upper left', ncol=3)
    pylab.grid()
    pylab.savefig('top-%06d.png' % bytes, dpi=72)

def main():
    records = libplot.parse()

    for bytes in libplot.unique(records, 'bytes'):
        plot(records, bytes)

    pylab.show()

if __name__ == '__main__':
    main()
