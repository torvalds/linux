#!/usr/bin/env python
import re, os, sys, argparse, multiprocessing

header_paths = [ "include/uapi/", "arch/lkl/include/uapi/",
                 "arch/lkl/include/generated/uapi/", "include/generated/" ]

headers = set()

def find_headers(path):
    headers.add(path)
    f = open(path)
    for l in f.readlines():
        m = re.search("#include <(.*)>", l)
        try:
            i = m.group(1)
            for p in header_paths:
                if os.access(p + i, os.R_OK):
                    if p + i not in headers:
                        headers.add(p + i)
                        find_headers(p + i)
        except:
            pass
    f.close()

def has_lkl_prefix(w):
    return w.startswith("lkl") or w.startswith("_lkl") or w.startswith("LKL") or \
        w.startswith("_LKL")

def find_symbols(regexp, store):
    for h in headers:
        f = open(h)
        for l in f.readlines():
            m = re.search(regexp, l)
            try:
                e = m.group(1)
                if not has_lkl_prefix(e):
                    store.add(e)
            except:
                pass
        f.close()

def find_ml_symbols(regexp, store):
    for h in headers:
        for i in re.finditer(regexp, open(h).read(), re.MULTILINE|re.DOTALL):
            for j in i.groups():
                store.add(j)

def lkl_prefix(w):
    r = ""

    if w.startswith("__"):
        r = "__"
    elif w.startswith("_"):
        r = "_"

    if w.isupper():
        r += "LKL"
    else:
        r += "lkl"

    if not w.startswith("_"):
        r += "_"

    r += w

    return r

def replace(h):
    content = open(h).read()
    content = re.sub("(#[ \t]*include[ \t]<)(.*>)", "\\1lkl/\\2", content,
                     flags = re.MULTILINE)
    for d in defines:
        search_str = "([^_a-zA-Z0-9]+)" + d + "([^_a-zA-Z0-9]+)"
        replace_str = "\\1" + lkl_prefix(d) + "\\2"
        content = re.sub(search_str, replace_str, content, flags = re.MULTILINE)
    for s in structs:
        search_str = "([^_a-zA-Z0-9]*struct\s+)" + s + "([^_a-zA-Z0-9]+)"
        replace_str = "\\1" + lkl_prefix(s) + "\\2"
        content = re.sub(search_str, replace_str, content, flags = re.MULTILINE)
    open(h, 'w').write(content)

parser = argparse.ArgumentParser(description='install lkl headers')
parser.add_argument('path', help='path to install to', )
parser.add_argument('-j', '--jobs', help='number of parallel jobs', default=1, type=int)
args = parser.parse_args()

find_headers("arch/lkl/include/uapi/asm/unistd.h")
headers.add("arch/lkl/include/uapi/asm/host_ops.h")

defines = set()
structs = set()

find_symbols("#[ \t]*define[ \t]*([_a-zA-Z]+[_a-zA-Z0-9]*)[^_a-zA-Z0-9]", defines)
find_symbols("typedef.*\s+([_a-zA-Z]+[_a-zA-Z0-9]*)\s*;", defines)
find_ml_symbols("typedef\s+struct\s*\{.*\}\s*([_a-zA-Z]+[_a-zA-Z0-9]*)\s*;", defines)
find_symbols("struct\s+([_a-zA-Z]+[_a-zA-Z0-9]*)\s*\{", structs)

def process_header(h):
    dir = os.path.dirname(h)
    out_dir = args.path + "/" + re.sub("(arch/lkl/include/uapi/|arch/lkl/include/generated/uapi/|include/uapi/|include/generated/uapi/|include/generated)(.*)", "lkl/\\2", dir)
    try:
        os.makedirs(out_dir)
    except:
        pass
    print "  INSTALL\t%s" % (out_dir + "/" + os.path.basename(h))
    os.system("scripts/headers_install.sh %s %s %s" % (out_dir, dir,
                                                       os.path.basename(h)))
    replace(out_dir + "/" + os.path.basename(h))

p = multiprocessing.Pool(args.jobs)
try:
    p.map_async(process_header, headers).wait(999999)
    p.close()
except:
    p.terminate()
finally:
    p.join()
