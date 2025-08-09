#include <stdlib.h>
#include <unistd.h>

system("git clone https://github.com/xi816/gc32-20020/");
chdir("gc32-20020/");
system("gcc core/ball.c -o ball");
system("./ball");
system("./prepare-disk disk.img");
system("./gc32-20020 -b bios.img -d disk.img");
