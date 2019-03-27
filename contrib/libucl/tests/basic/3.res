packagesite = "http://pkg-test.freebsd.org/pkg-test/unknown/latest";
squaretest = "some[]value";
alias {
    all-depends = "query %dn-%dv";
    annotations = "info -A";
    build-depends = "info -qd";
    download = "fetch";
    iinfo = "info -i -g -x";
    isearch = "search -i -g -x";
    leaf = "query -e '%a == 0' '%n-%v'";
    leaf = "query -e '%a == 0' '%n-%v'";
    list = "info -ql";
    origin = "info -qo";
    provided-depends = "info -qb";
    raw = "info -R";
    required-depends = "info -qr";
    shared-depends = "info -qB";
    show = "info -f -k";
    size = "info -sq";
}
repo_dirs [
    "/home/bapt",
    "/usr/local/etc",
]

