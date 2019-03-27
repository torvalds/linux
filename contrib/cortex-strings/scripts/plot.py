"""Plot the results for each test.  Spits out a set of images into the
current directory.
"""

import libplot

import fileinput
import collections
import pprint

import pylab

Record = collections.namedtuple('Record', 'variant test size loops src_alignment dst_alignment run_id rawtime comment time bytes rate')

def unique(rows, name):
    """Takes a list of values, pulls out the named field, and returns
    a list of the unique values of this field.
    """
    return sorted(set(getattr(x, name) for x in rows))

def to_float(v):
    """Convert a string into a better type.

    >>> to_float('foo')
    'foo'
    >>> to_float('1.23')
    1.23
    >>> to_float('45')
    45
    """
    try:
        if '.' in v:
            return float(v)
        else:
            return int(v)
    except:
        return v

def parse():
    # Split the input up
    rows = [x.strip().split(':') for x in fileinput.input()]
    # Automatically turn numbers into the base type
    rows = [[to_float(y) for y in x] for x in rows]

    # Scan once to calculate the overhead
    r = [Record(*(x + [0, 0, 0])) for x in rows]
    bounces = pylab.array([(x.loops, x.rawtime) for x in r if x.test == 'bounce'])
    fit = pylab.polyfit(bounces[:,0], bounces[:,1], 1)

    records = []

    for row in rows:
        # Make a dummy record so we can use the names
        r1 = Record(*(row + [0, 0, 0]))

        bytes = r1.size * r1.loops
        # Calculate the bounce time
        delta = pylab.polyval(fit, [r1.loops])
        time = r1.rawtime - delta
        rate = bytes / time

        records.append(Record(*(row + [time, bytes, rate])))

    return records

def plot(records, field, scale, ylabel):
    variants = unique(records, 'variant')
    tests = unique(records, 'test')

    colours = libplot.make_colours()

    # A little hack.  We want the 'all' record to be drawn last so
    # that it's obvious on the graph.  Assume that no tests come
    # before it alphabetically
    variants.reverse()

    for test in tests:
        for variant in variants:
            v = [x for x in records if x.test==test and x.variant==variant]
            v.sort(key=lambda x: x.size)
            V = pylab.array([(x.size, getattr(x, field)) for x in v])

            # Ensure our results appear
            order = 1 if variant == 'this' else 0

            try:
                # A little hack.  We want the 'all' to be obvious on
                # the graph
                if variant == 'all':
                    pylab.scatter(V[:,0], V[:,1]/scale, label=variant)
                    pylab.plot(V[:,0], V[:,1]/scale)
                else:
                    pylab.plot(V[:,0], V[:,1]/scale, label=variant,
                            zorder=order, c = colours.next())

            except Exception, ex:
                # michaelh1 likes to run this script while the test is
                # still running which can lead to bad data
                print ex, 'on %s of %s' % (variant, test)

        pylab.legend(loc='lower right', ncol=2, prop={'size': 'small'})
        pylab.xlabel('Block size (B)')
        pylab.ylabel(ylabel)
        pylab.title('%s %s' % (test, field))
        pylab.grid()

        pylab.savefig('%s-%s.png' % (test, field), dpi=100)
        pylab.semilogx(basex=2)
        pylab.savefig('%s-%s-semilog.png' % (test, field), dpi=100)
        pylab.clf()

def test():
    import doctest
    doctest.testmod()

def main():
    records = parse()

    plot(records, 'rate', 1024**2, 'Rate (MB/s)')
    plot(records, 'time', 1, 'Total time (s)')

if __name__ == '__main__':
    main()
