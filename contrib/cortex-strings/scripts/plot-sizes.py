#!/usr/bin/env python

"""Plot the performance for different block sizes of one function across
variants.
"""

import libplot

import pylab
import pdb
import math

def pretty_kb(v):
    if v < 1024:
        return '%d' % v
    else:
        if v % 1024 == 0:
            return '%d k' % (v//1024)
        else:
            return '%.1f k' % (v/1024)

def plot(records, function, alignment=None, scale=1):
    variants = libplot.unique(records, 'variant', prefer='this')
    records = [x for x in records if x.function==function]

    if alignment != None:
        records = [x for x in records if x.src_alignment==alignment[0] and
                   x.dst_alignment==alignment[1]]

    alignments = libplot.unique(records, ('src_alignment', 'dst_alignment'))
    if len(alignments) != 1:
        return False
    if libplot.alignments_equal(alignments):
        aalignment = alignments[0][0]
    else:
        aalignment = "%s:%s" % (alignments[0][0], alignments[0][1])

    bytes = libplot.unique(records, 'bytes')[0]

    colours = libplot.make_colours()
    all_x = []

    pylab.figure(1).set_size_inches((6.4*scale, 4.8*scale))
    pylab.clf()

    if 'str' in function:
        # The harness fills out to 16k.  Anything past that is an
        # early match
        top = 16384
    else:
        top = 2**31

    for variant in variants:
        matches = [x for x in records if x.variant==variant and x.bytes <= top]
        matches.sort(key=lambda x: x.bytes)

        X = sorted(list(set([x.bytes for x in matches])))
        Y = []
        Yerr = []
        for xbytes in X:
            vals = [x.bytes*x.loops/x.elapsed/(1024*1024) for x in matches if x.bytes == xbytes]
            if len(vals) > 1:
                mean = sum(vals)/len(vals)
                Y.append(mean)
                if len(Yerr) == 0:
                    Yerr = [[], []]
                err1 = max(vals) - mean
                assert err1 >= 0
                err2 = min(vals) - mean
                assert err2 <= 0
                Yerr[0].append(abs(err2))
                Yerr[1].append(err1)
            else:
                Y.append(vals[0])

        all_x.extend(X)
        colour = colours.next()

        if X:
            pylab.plot(X, Y, c=colour)
            if len(Yerr) > 0:
                pylab.errorbar(X, Y, yerr=Yerr, c=colour, label=variant, fmt='o')
            else:
                pylab.scatter(X, Y, c=colour, label=variant, edgecolors='none')

    pylab.legend(loc='upper left', ncol=3, prop={'size': 'small'})
    pylab.grid()
    pylab.title('%(function)s of %(aalignment)s byte aligned blocks' % locals())
    pylab.xlabel('Size (B)')
    pylab.ylabel('Rate (MB/s)')

    # Figure out how high the range goes
    top = max(all_x)

    power = int(round(math.log(max(all_x)) / math.log(2)))

    pylab.semilogx()

    pylab.axes().set_xticks([2**x for x in range(0, power+1)])
    pylab.axes().set_xticklabels([pretty_kb(2**x) for x in range(0, power+1)])
    pylab.xlim(0, top)
    pylab.ylim(0, pylab.ylim()[1])
    return True

def main():
    records = libplot.parse()

    functions = libplot.unique(records, 'function')
    alignments = libplot.unique(records, ('src_alignment', 'dst_alignment'))

    for function in functions:
        for alignment in alignments:
            for scale in [1, 2.5]:
                if plot(records, function, alignment, scale):
                    pylab.savefig('sizes-%s-%02d-%02d-%.1f.png' % (function, alignment[0], alignment[1], scale), dpi=72)

    pylab.show()

if __name__ == '__main__':
    main()
