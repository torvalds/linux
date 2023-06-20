#!/usr/bin/env python3
import re, os, sys, argparse, multiprocessing, fnmatch

srctree = os.environ["srctree"]
objtree = os.environ["objtree"]
header_paths = [ "include/uapi/", "arch/lkl/include/uapi/",
                 "arch/lkl/include/generated/uapi/", "include/generated/" ]

headers = set()
includes = set()

def relpath2abspath(relpath):
    if "generated" in relpath:
        return objtree + "/" + relpath
    else:
        return srctree + "/" + relpath

def find_headers(path):
    headers.add(path)
    f = open(relpath2abspath(path))
    for l in f.readlines():
        m = re.search("#include <(.*)>", l)
        try:
            i = m.group(1)
            for p in header_paths:
                if os.access(relpath2abspath(p + i), os.R_OK):
                    if p + i not in headers:
                        includes.add(i)
                        headers.add(p + i)
                        find_headers(p + i)
        except:
            pass
    f.close()

def has_lkl_prefix(w):
  return w.startswith("lkl") or w.startswith("_lkl") or w.startswith("__lkl") \
         or w.startswith("LKL") or w.startswith("_LKL") or w.startswith("__LKL")

def find_symbols(regexp, store):
    for h in headers:
        f = open(h)
        for l in f.readlines():
            m = regexp.search(l)
            if not m:
                continue
            for e in reversed(m.groups()):
                if e:
                    if not has_lkl_prefix(e):
                        store.add(e)
                    break
        f.close()

def find_ml_symbols(regexp, store):
    for h in headers:
        for i in regexp.finditer(open(h).read()):
            for j in reversed(i.groups()):
                if j:
                    if not has_lkl_prefix(j):
                        store.add(j)
                    break

def find_enums(block_regexp, symbol_regexp, store):
    for h in headers:
        # remove comments
        content = re.sub(re.compile("(\/\*(\*(?!\/)|[^*])*\*\/)", re.S|re.M), " ", open(h).read())
        # remove preprocesor lines
        clean_content = ""
        for l in content.split("\n"):
            if re.match("\s*#", l):
                continue
            clean_content += l + "\n"
        for i in block_regexp.finditer(clean_content):
            for j in reversed(i.groups()):
                if j:
                    for k in symbol_regexp.finditer(j):
                        for l in k.groups():
                            if l:
                                if not has_lkl_prefix(l):
                                    store.add(l)
                                break

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
    for i in includes:
        search_str = "(#[ \t]*include[ \t]*[<\"][ \t]*)" + i + "([ \t]*[>\"])"
        replace_str = "\\1" + "lkl/" + i + "\\2"
        content = re.sub(search_str, replace_str, content)
    tmp = ""
    for w in re.split("(\W+)", content):
        if w in defines:
            w = lkl_prefix(w)
        tmp += w
    content = tmp
    for s in structs:
        # XXX: cleaner way?
        if s == 'TAG':
            continue
        search_str = "(\W?struct\s+)" + s + "(\W)"
        replace_str = "\\1" + lkl_prefix(s) + "\\2"
        content = re.sub(search_str, replace_str, content, flags = re.MULTILINE)
    for s in unions:
        search_str = "(\W?union\s+)" + s + "(\W)"
        replace_str = "\\1" + lkl_prefix(s) + "\\2"
        content = re.sub(search_str, replace_str, content, flags = re.MULTILINE)
    open(h, 'w').write(content)

parser = argparse.ArgumentParser(description='install lkl headers')
parser.add_argument('path', help='path to install to', )
parser.add_argument('-j', '--jobs', help='number of parallel jobs', default=1, type=int)
args = parser.parse_args()

find_headers("arch/lkl/include/uapi/asm/syscalls.h")
headers.add("arch/lkl/include/uapi/asm/host_ops.h")
find_headers("include/uapi/linux/uhid.h")
find_headers("include/uapi/linux/input-event-codes.h")

if 'LKL_INSTALL_ADDITIONAL_HEADERS' in os.environ:
    with open(os.environ['LKL_INSTALL_ADDITIONAL_HEADERS'], 'rU') as f:
        for line in f.readlines():
            line = line.split('#', 1)[0].strip()
            if line != '':
                headers.add(line)

new_headers = set()

for h in headers:
    dir = os.path.dirname(h)
    out_dir = args.path + "/" + re.sub("(arch/lkl/include/uapi/|arch/lkl/include/generated/uapi/|include/uapi/|include/generated/uapi/|include/generated)(.*)", "lkl/\\2", dir)
    try:
        os.makedirs(out_dir)
    except:
        pass
    print("  INSTALL\t%s" % (out_dir + "/" + os.path.basename(h)))
    os.system(srctree+"/scripts/headers_install.sh %s %s" % (os.path.abspath(h),
                                                       out_dir + "/" + os.path.basename(h)))
    new_headers.add(out_dir + "/" + os.path.basename(h))

headers = new_headers

defines = set()
structs = set()
unions = set()

p = re.compile("#[ \t]*define[ \t]*(\w+)")
find_symbols(p, defines)
p = re.compile("typedef.*(\(\*(\w+)\)\(.*\)\s*|\W+(\w+)\s*|\s+(\w+)\(.*\)\s*);")
find_symbols(p, defines)
p = re.compile("typedef\s+(struct|union)\s+\w*\s*{[^\\{\}]*}\W*(\w+)\s*;", re.M|re.S)
find_ml_symbols(p, defines)
defines.add("siginfo_t")
defines.add("sigevent_t")
p = re.compile("struct\s+(\w+)\s*\{")
find_symbols(p, structs)
structs.add("iovec")
p = re.compile("union\s+(\w+)\s*\{")
find_symbols(p, unions)
p = re.compile("static\s+__inline__(\s+\w+)+\s+(\w+)\([^)]*\)\s")
find_symbols(p, defines)
p = re.compile("static\s+__always_inline(\s+\w+)+\s+(\w+)\([^)]*\)\s")
find_symbols(p, defines)
p = re.compile("enum\s+(\w*)\s*{([^}]*)}", re.M|re.S)
q = re.compile("(\w+)\s*(,|=[^,]*|$)", re.M|re.S)
find_enums(p, q, defines)

# needed for i386
defines.add("__NR_stime")

def process_header(h):
    print("  REPLACE\t%s" % (out_dir + "/" + os.path.basename(h)))
    replace(h)

p = multiprocessing.Pool(args.jobs)
try:
    p.map_async(process_header, headers).wait(999999)
    p.close()
except:
    p.terminate()
finally:
    p.join()
