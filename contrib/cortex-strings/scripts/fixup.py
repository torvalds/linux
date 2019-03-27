"""Simple script that enables target specific blocks based on the first argument.

Matches comment blocks like this:

/* For Foo: abc
def
*/

and de-comments them giving:
abc
def
"""
import re
import sys

def main():
    key = sys.argv[1]
    expr = re.compile(r'/\* For %s:\s([^*]+)\*/' % key, re.M)

    for arg in sys.argv[2:]:
        with open(arg) as f:
            body = f.read()
        with open(arg, 'w') as f:
            f.write(expr.sub(r'\1', body))

if __name__ == '__main__':
    main()
