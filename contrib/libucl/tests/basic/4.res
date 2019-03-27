name = "pkgconf";
version = "0.9.3";
origin = "devel/pkgconf";
comment = "Utility to help to configure compiler and linker flags";
arch = "freebsd:9:x86:64";
maintainer = "bapt@FreeBSD.org";
prefix = "/usr/local";
licenselogic = "single";
licenses [
    "BSD",
]
flatsize = 60523;
desc = <<EOD
pkgconf is a program which helps to configure compiler and linker flags for
development frameworks. It is similar to pkg-config, but was written from
scratch in Summer of 2011 to replace pkg-config, which now needs itself to build
itself.

WWW: https://github.com/pkgconf/pkgconf
EOD;
categories [
    "devel",
]
files {
    /usr/local/bin/pkg-config = "-";
    /usr/local/bin/pkgconf = "4a0fc53e5ad64e8085da2e61652d61c50b192a086421d865703f1de9f724da38";
    /usr/local/share/aclocal/pkg.m4 = "cffab33d659adfe36497ec57665eec36fa6fb7b007e578e6ac2434cc28be8820";
    /usr/local/share/licenses/pkgconf-0.9.3/bsd = "85e7a53b5e2d3e350e2d084fed2f94b7f63005f8e1168740e1e84aa9fa5d48ce";
    /usr/local/share/licenses/pkgconf-0.9.3/license = "d9cce0db43502eb1bd8fbef7e960cfaa43b5647186f7f7379923b336209fd77b";
    /usr/local/share/licenses/pkgconf-0.9.3/catalog.mk = "e7b131acce7c3d3c61f2214607b11b34526e03b05afe89a608f50586a898e2ef";
}
directories {
    /usr/local/share/licenses/pkgconf-0.9.3/ = false;
    /usr/local/share/licenses/ = true;
}
scripts {
    post-install = "cd /usr/local\nn";
    pre-deinstall = "cd /usr/local\nn";
    post-deinstall = "cd /usr/local\nn";
}
multiline-key = <<EOD
test
test
test\n
/* comment like */
# Some invalid endings
 EOD
EOD   
EOF
# Valid ending + empty string

EOD;
normal-key = "<<EODnot";

