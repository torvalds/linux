#!/usr/bin/env python3
import re, os, sys, argparse, multiprocessing, fnmatch

class Installer:
    def __init__(self, install_path):
        self.srctree = os.environ["srctree"]
        self.objtree = os.environ["objtree"]
        self.header_paths = [ "include/uapi/", "arch/lkl/include/uapi/",
                              "arch/lkl/include/generated/uapi/", "include/generated/" ]
        self.headers = set()
        self.includes = set()
        self.defines = set()
        self.structs = set()
        self.unions = set()
        self.install_path = install_path

    def relpath2abspath(self, relpath):
        if "generated" in relpath:
            return self.objtree + "/" + relpath
        else:
            return self.srctree + "/" + relpath

    def find_headers(self, path):
        self.headers.add(path)
        with open(self.relpath2abspath(path)) as f:
            for l in f.readlines():
                m = re.search("#include <(.*)>", l)
                try:
                    i = m.group(1)
                    for p in self.header_paths:
                        if os.access(self.relpath2abspath(p + i), os.R_OK):
                            if p + i not in self.headers:
                                self.includes.add(i)
                                self.headers.add(p + i)
                                self.find_headers(p + i)
                except:
                    pass

    def has_lkl_prefix(self, w):
        return w.startswith("lkl") or w.startswith("_lkl") or \
            w.startswith("__lkl")  or w.startswith("LKL") or \
            w.startswith("_LKL") or w.startswith("__LKL") or \
            w.startswith("__attribute")

    def find_symbols(self, regexp, store):
        for h in self.headers:
            with open(h) as f:
                for l in f.readlines():
                    m = regexp.search(l)
                    if not m:
                        continue
                    for e in reversed(m.groups()):
                        if e:
                            if not self.has_lkl_prefix(e):
                                store.add(e)
                            break

    def find_ml_symbols(self, regexp, store):
        for h in self.headers:
            for i in regexp.finditer(open(h).read()):
                for j in reversed(i.groups()):
                    if j:
                        if not self.has_lkl_prefix(j):
                            store.add(j)
                        break

    def find_enums(self, block_regexp, symbol_regexp, store):
        for h in self.headers:
            # remove comments
            content = re.sub(re.compile(r"(\/\*(\*(?!\/)|[^*])*\*\/)", re.S|re.M), " ", open(h).read())
            # remove preprocesor lines
            clean_content = ""
            for l in content.split("\n"):
                if re.match(r"\s*#", l):
                    continue
                clean_content += l + "\n"
            for i in block_regexp.finditer(clean_content):
                for j in reversed(i.groups()):
                    if j:
                        for k in symbol_regexp.finditer(j):
                            for l in k.groups():
                                if l:
                                    if not self.has_lkl_prefix(l):
                                        store.add(l)
                                    break

    def lkl_prefix(self, w):
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

    def install_headers(self):
        self.find_headers("arch/lkl/include/uapi/asm/syscalls.h")
        self.headers.add("arch/lkl/include/uapi/asm/host_ops.h")
        self.find_headers("include/uapi/linux/android/binder.h")
        self.find_headers("include/uapi/linux/uhid.h")
        self.find_headers("include/uapi/linux/mman.h")
        self.find_headers("include/uapi/linux/input-event-codes.h")

        if 'LKL_INSTALL_ADDITIONAL_HEADERS' in os.environ:
            with open(os.environ['LKL_INSTALL_ADDITIONAL_HEADERS'], 'rU') as f:
                for line in f.readlines():
                    line = line.split('#', 1)[0].strip()
                    if line != '':
                        self.headers.add(line)

        new_headers = set()

        for h in self.headers:
            dir = os.path.dirname(h)
            out_dir = args.path + "/" + re.sub("(arch/lkl/include/uapi/|arch/lkl/include/generated/uapi/|include/uapi/|include/generated/uapi/|include/generated)(.*)", "lkl/\\2", dir)
            try:
                os.makedirs(out_dir)
            except:
                pass
            print("  INSTALL\t%s" % (out_dir + "/" + os.path.basename(h)))
            os.system(self.srctree+"/scripts/headers_install.sh %s %s" % (self.relpath2abspath(h),
                                                       out_dir + "/" + os.path.basename(h)))
            new_headers.add(out_dir + "/" + os.path.basename(h))

        self.headers = new_headers

    def find_all_symbols(self):
        p = re.compile(r"#[ \t]*define[ \t]*(\w+)")
        self.find_symbols(p, self.defines)
        p = re.compile(r"typedef.*(\(\*(\w+)\)\(.*\)\s*|\W+(\w+)\s*|\s+(\w+)\(.*\)\s*);")
        self.find_symbols(p, self.defines)
        p = re.compile(r"typedef\s+(struct|union)\s+\w*\s*{[^\\{\}]*}\W*(\w+)\s*;", re.M|re.S)
        self.find_ml_symbols(p, self.defines)
        self.defines.add("siginfo_t")
        self.defines.add("sigevent_t")
        p = re.compile(r"struct\s+(\w+)\s*\{")
        self.find_symbols(p, self.structs)
        self.structs.add("iovec")
        p = re.compile(r"union\s+(\w+)\s*\{")
        self.find_symbols(p, self.unions)
        p = re.compile(r"static\s+__inline__(\s+\w+)+\s+(\w+)\([^)]*\)\s")
        self.find_symbols(p, self.defines)
        p = re.compile(r"static\s+__always_inline(\s+\w+)+\s+(\w+)\([^)]*\)\s")
        self.find_symbols(p, self.defines)
        p = re.compile(r"enum\s+(\w*)\s*{([^}]*)}", re.M|re.S)
        q = re.compile(r"(\w+)\s*(,|=\s*\w+\s*\([^()]*\)|=[^,]*|$)", re.M|re.S)
        self.find_enums(p, q, self.defines)

        # needed for i386
        self.defines.add("__NR_stime")

    def update_header(self, h):
        print("  REPLACE\t%s" % h)
        content = open(h).read()
        for i in self.includes:
            search_str = r"(#[ \t]*include[ \t]*[<\"][ \t]*)" + i + r"([ \t]*[>\"])"
            replace_str = "\\1" + "lkl/" + i + "\\2"
            content = re.sub(search_str, replace_str, content)
        tmp = ""
        for w in re.split(r"(\W+)", content):
            if w in self.defines:
                w = self.lkl_prefix(w)
            tmp += w
        content = tmp
        for s in self.structs:
            # XXX: cleaner way?
            if s == 'TAG':
                continue
            search_str = r"(\W?struct\s+)" + s + r"(\W)"
            replace_str = "\\1" + self.lkl_prefix(s) + "\\2"
            content = re.sub(search_str, replace_str, content, flags = re.MULTILINE)
        for s in self.unions:
            search_str = r"(\W?union\s+)" + s + r"(\W)"
            replace_str = "\\1" + self.lkl_prefix(s) + "\\2"
            content = re.sub(search_str, replace_str, content, flags = re.MULTILINE)
        open(h, 'w').write(content)

    def update_headers(self):
        p = multiprocessing.Pool(args.jobs)
        try:
            p.map_async(installer.update_header, installer.headers).wait(999999)
            p.close()
        except:
            p.terminate()
        finally:
            p.join()

if __name__ == '__main__':
    multiprocessing.freeze_support()

    parser = argparse.ArgumentParser(description='install lkl headers')
    parser.add_argument('path', help='path to install to', )
    parser.add_argument('-j', '--jobs', help='number of parallel jobs', default=1, type=int)
    args = parser.parse_args()

    installer = Installer(args.path)
    installer.install_headers()
    installer.find_all_symbols()
    installer.update_headers()
