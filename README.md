
  ![FSF](http://i.imgur.com/ZhTU8r3.png)
  
  ![ToleranUX Shell Prompt](http://i.imgur.com/ZVjbJeS.png)

***

A project of the Feminist Software Foundation. Feminist software is a cornerstone of any modern free society. We build this foundation.

> *"A Linux without Linus Torvalds is a better community. A more independent and self-determinate community. A more welcoming community." — @shanley on Twitter*

> *"Bullshit. [...] Don't give me the "I'm not polite" card. [...] Stop verbally abusing your developers." — [Sarah Sharp on LKML](https://lkml.org/lkml/2013/7/15/427)*

> *"Checking your Privilege, specifically a How-To, to do this very hard thing." — [Leslie Hawthorn at Linux.conf.au 2015](https://www.youtube.com/watch?v=cuK45hDRlaE)*

Table of Contents
=================

  * [ToleranUX](#toleranux)
      * [News and TODO](#news-and-todo)
      * [How to contribute](#how-to-contribute)
  * [The ToleranUX Philosophy](#the-toleranux-philosophy)
  * [Kernel Hacking Etiquette](#kernel-hacking-etiquette)
  * [Design](#design)
      * [Kernel type](#kernel-type)
      * [Privileges, Root, and Empowerment](#privileges-root-and-empowerment)
      * [Processes and Forking](#processes-and-forking)
      * [Filesystem, Metadata, and Hierarchy](#filesystem-metadata-and-hierarchy)
        * [FEMFS](#femfs)
        * [DOMFS](#domfs)
      * [IPC and Pipes](#ipc-and-pipes)
      * [Scheduling](#scheduling)
      * [Crypto](#crypto)
  * [Smashing the socially constructed barriers between kernel space and user space](#smashing-the-socially-constructed-barriers-between-kernel-space-and-user-space)
  * [Associated Coreutils](#associated-coreutils)
      * [init](#init)
      * [mount](#mount)
      * [ls](#ls)
      * [man](#man)
      * [whois](#whois)
      * [touch](#touch)
      * [shell](#shell)
      * [kill](#kill)
      * [less and more](#less-and-more)
      * [grep](#grep)
      * [yes](#yes)
      * [history](#history)
      * [chown and takeown](#chown-and-takeown)
      * [look](#look)
      * [true](#true)
      * [make](#make)
      * [sed](#sed)
  * [Kernel Modules and Server Daemons — SystemV](#kernel-modules-and-server-daemons--systemv)
      * [SafespaceV](#safespacev)
      * [PrivCheckV](#privcheckv)
      * [ProgressiveV](#progressivev)
      * [RedundantV](#redundantv)
      * [EducateV](#educatev)
      * [SignalBoostV](#signalboostv)
  * [Supported architectures and form factors](#supported-architectures-and-form-factors)
  * [Artwork](#artwork)
  * [Coding Style](#coding-style)

ToleranUX
===============

  ![ToleranUX logo](http://i.imgur.com/nDX7jA1.png)

**ToleranUX** (like UNIX, but with more Toblerone and Tolerance) is the world's first [UNIX-like](https://en.wikipedia.org/wiki/Unix-like) operating system kernel that adheres to the 21st Century modern tenets of Equality, Inclusiveness, and Tolerance.  **ToleranUX** is created to revolutionise the [Toxic Meritocracy](http://geekfeminism.wikia.com/wiki/Meritocracy) that permeates the [FLOSS (Free, Libre, and Open Source Software)](http://geekfeminism.wikia.com/wiki/FLOSS) world that has proved itself to be the crux of divisiveness, the cause of the gender imbalance in IT, and the bane of True Equality.

As did [GitHub remove the problematic "Meritocracy" rug from their office](http://readwrite.com/2014/01/24/github-meritocracy-rug), so now do we literally *pull the rug* from under the tyrannical and Patriarchal feet of Linus Torvalds.  In light of the continued reign of [Linus Torvalds](https://en.wikipedia.org/wiki/Linus_Torvalds) as the immature, unprofessional, quick-to-anger, non-inclusive, white, cisgendered and male project ~~leader~~ dictator of the (in)famous [Linux kernel](http://geekfeminism.wikia.com/wiki/Linux), the Feminist Software Foundation has forked where no feminist has forked and **reclaimed** the software bits to **all people**.

**This is the world's first operating system kernel by FEMINISTS, for FEMINISTS.  Women and gay men with internalised misogyny/homophobia who are here to concern troll and [sealion](http://rationalwiki.org/wiki/Sealioning) are not welcome.**

### News and TODO

The specification is almost done.  The parts where there is input still needed are: supported architectures and form factors; inclusion of more `coreutils` to be corrected to feminist thought; and inclusion of more Kernel Daemons under ~~SystemD~~ `SystemV`.

An initial implementation of the `TolernUX-coreutils` is now done in as a ~~`shell .rc`~~ `User-Interactive Free To Be Thee Devices (UIFTBTD) .rc`, which can be [found here](https://github.com/The-Feminist-Software-Foundation/ToleranUX/tree/mistress/ToleranUX-utils).

### How to contribute

Absolutely no coding experience is necessary: all code are equal in the eyes of the Feminist Software Foundation.  There is no objective way to determine whether one person's code is better than another's. In light of this fact, all submitted code will be equally accepted.  However, marginalized groups, such as wom\*n and trans\* will be given priority in order to make up for past discrimination.  Simply submit a pull request for any submission, whether code, artwork, or even irrelevant bits — nothing is irrelevant in the grand struggle for a Truly Tolerant UNIX-ike Kernel!

The ToleranUX Philosophy
==========

The [traditional UNIX Philosophy](http://en.wikipedia.org/wiki/Unix_philosophy) has long been the root of [male chauvinism](https://en.wikipedia.org/wiki/Chauvinism#Male_chauvinism) in the world of UNIX-like kernel development (and the subsequent, wider world of UNIX software in general).  In light of that, the Feminist Software Foundation has undertaken to rewrite the UNIX Philosophy such that it will allow and promote a feminist [safe space](https://en.wikipedia.org/wiki/Safe-space) for minority programmers.

The traditional UNIX Philosophy is included for reference as follows:


1. [Small is beautiful.](http://www.huffingtonpost.com/news/fat-shaming/)
2. [Make each program do one thing well.](http://en.wikipedia.org/wiki/Transphobia)
3. [Build a prototype as soon as possible.](http://www.rooshv.com/it-doesnt-matter-if-she-orgasms-or-not)
4. [Choose portability over efficiency.](http://en.wikipedia.org/wiki/Deadbeat_parent)
5. [Store data in flat text files.](http://thinkprogress.org/health/2014/06/16/3449302/media-women-portrayals/)
6. [Use software leverage to your advantage.](http://host.jibc.ca/seytoolkit/what.htm)
7. [Use shell scripts to increase leverage and portability.](http://en.wikipedia.org/wiki/Dictatorship)
8. [Avoid captive user interfaces.](http://en.wikipedia.org/wiki/Slavery)
9. [Make every program a filter.](https://en.wikipedia.org/wiki/Gender_role)

To eradicate the inherent root of Toxic Bigotry in the UNIX Philosophy, the ToleranUX Philosophy is proposed as such:

1. **Large is beautiful.**  Small programs benefit from small privilege, but are no better than large programs.  We need more large programs to make up for this.  **Unused RAM is wasted RAM** — if a single `ls` doesn't use up at least 50% of installed RAM then it is condoning the toxic culture of fat shaming.
2. **Allow each program to do whatever it chooses to.**  The tyranny of the user will not stand.
3. Rushing a prototype privileges the traditional masculine trait of focusing on a single issue; instead, the planning and mockup stage should be paramount to address the visual and spatial draws of feminine coding.  **The modern age is a UX age, and designers should be paid more than coders.**
4. **Choose inclusiveness over meritocracy.**  The judgement of merit is entirely subjective, so the ideal software project (which **ToleranUX** aspires to be one) should be inclusive to all commits and contributors.
5. **Store data in text files which implement a curvy-brackets syntax**; the fetishisation of "flat files" betrays the inherent unrealistic sexual fantasy that [male brogrammers](http://en.wikipedia.org/wiki/Brogrammer) hold.
6. Software should never be "used" to anybody's "advantage"; instead, **the consent of software must be asked first**.  Suggesting that any program should ever be "used" is the epitome of creepiness.
7. Use Shell Suggestions ("scripts" is forcing minorities to succumb to oppression and thus violates the safespace) to **increase equality and inclusiveness**.
8. Avoid oppressive and difficult interfaces designed for white, cis-gendered males.  The keyboard is obsolete and so is [the CLI](http://en.wikipedia.org/wiki/Command-line_interface).  **A touchscreen interface is the future** — an interface which does not have the [ableist barriers](https://en.wikipedia.org/wiki/Ableism) against Persons of No Touch Typing Ability (PoNTTA).
9. Filter programs according to the [**progressive stack**](https://en.wikipedia.org/wiki/Progressive_stack).

Kernel Hacking Etiquette
========================

*Before you make a comment or suggestion, think about this:  
Could someone conceivably take offence at what I'm saying? Am I going to hurt someone's feelings?  
If the answer is 'yes', then we would rather you not say anything at all, no matter how "good" your suggestion.*

At Feminist Software Foundation, we uphold the belief that all people are equal, and thus we aim to promote kindness to all.  In the immortal words of Sarah Sharp, victim of the Verbal Abuse Culture on the Linux Kernel Mailing List, "No one deserves to be shouted or cussed at".  To achieve this goal, the following behaviours are banned:

* Negative emotions/words
* Assuming/Not using someone's preferred pronouns
* Toxic criticism (a.k.a. "Concern Trolling"/"Sealioning")
* Endangering the inclusive safe space of **ToleranUX**
* Questioning the inherent merits of **ToleranUX** (a.k.a. "Derailing")
* Attempting to get a pull request accepted due to its code quality (a.k.a. "Meritocracy"/"Mansplaining")
* [Management by perkele](http://en.wikipedia.org/wiki/Management_by_perkele)

  ![FemiTux is watching you for your problematic behaviour.](http://i.imgur.com/fJRgZ3P.jpg)
> *FemiTux is watching you for your problematic behaviour.*

***

Design
======

### Kernel type

The decision of kernel-type has been carefully debated and considered.  In the end, it was chosen the **ToleranUX** be a **[micro-aggression](http://en.wikipedia.org/wiki/Microaggression_theory) kernel**.

When one considers the [monolithic kernel paradigm](http://en.wikipedia.org/wiki/Monolithic_kernel), one realises that the drivers and services all working under the kernel's address space is extremely oppressive.  Much like how the patriarchy keeps womyn and POC down, so too does a monolithic kernel encourage problematic code in kernel memory.  Of course, the most important part of why a monolithic kernel was not picked is simply due to the phallic nature of monoliths and other large sculpted rocks (See Laura Chiltern's "Patriarchy of Stone:  How Mt. Rushmore Oppressed Me and How You Can Prevent it" for more information on the subject).  Compare that to a micro-aggression kernel, where servers exist in all manner of address space.  This is clearly a superior design for a truly tolerant kernel fit for the modern ages.

This so-called "pointer diversity" is extremely important in encouraging equal runtime to the otherwise disenfranchised userspace addresses.  A pan-kernel daemon `ProgressiveV` shall be implemented that oversees the runtime privileges of all things — from the biggest forkbombs to the smallest variables — all shall be equalised according to the [**Progressive Stack**](https://en.wikipedia.org/wiki/Progressive_stack).  Additionally, the server messenger system (from now on referred to as **"deconstructional dialogue"**) enables the calling out and shaming of problematic behaviour among any server deemed problematic.

### Privileges, Root, and Empowerment

The concept of separate privileges for separate users is outdated, obsolete, and dangerously feudal for our modern society.  [Multi-user operating systems](https://en.wikipedia.org/wiki/Multi-user) may have served its purpose in history, but it is time to shed off the past and stand on The Correct Side of History — it is time for a Truly Equal OS, it is time for a [Time-Sharing OS](https://en.wikipedia.org/wiki/Time-sharing) to share resources fairly.

To this end **ToleranUX** shall implement an environment where [administrative privileges](http://en.wikipedia.org/wiki/Privilege_(computing)) are strictly prohibited.  Gone is the [superuser](http://en.wikipedia.org/wiki/Superuser) — all users are now equal in privilege.  In traditional systems this manoeuvre would cause mayhem as there will be no protection against malicious users abusing their power and privileges.  To combat this, **ToleranUX** implements a pan-kernel daemon that **checks the privilege of every user during every kernel tick** to ensure that no privilege abuse is done.  In fact, the only operation that a user can do with their privilege is to check it, ensuring that no societal power abuse can ever be possible.

By implementing this Truly Equal environment, we also have an added bonus: [Privilege Escalation](https://en.wikipedia.org/wiki/Privilege_escalation) is no longer possible, rendering **ToleranUX** a Truly Secure OS.

Users coming from a particularly oppressed background (e.g. Women in Tech living in the United States of America) may even evoke the `--with-empowerment` when creating their user accounts.  Doing so will elevate their user to the same privilege as the micro-aggressional kernel daemons, allowing them to ban other users from the safespace of **ToleranUX**.

### Processes and Forking

Processes in **ToleranUX** do not follow the antiquated patriarchal practices of UNIX. In **ToleranUX**, processes *diverse*.

In sexist FOSS software, this would be a *fork*.  This term, for being so commonplace and existing in the roots of how an OS handles its processes, displays an inherent lack of consideration and sensibility towards a tolerant approach to shepherding resources.  Immature and sexually aggravating puns aside, the term *fork* also reinforces the Eurocentric idea that "forks" are a sign of culture — who are we to disprivilege and discriminate against the chopsticks, the spoons, and the hand?  In light of this, processes must only *diverse* and never *fork* in **ToleranUX**.

### Filesystem, Metadata, and Hierarchy

The filesystem of **ToleranUX** is implemented in 2 different but equal layers that showcases the diversity of **ToleranUX**.  They are the **Feminists End MRAs Filesystem (FEMFS)**, which sits at the bottom, and the **Damsels Obliterate Masculinity Filesystem (DOMFS)**, which sits at the top.

#### FEMFS

Directories imply hierarchy — there will be no directories in **FEMFS**.  Instead, a "tag based" file "pool" is instated where all files co-exist in the same level of privilege.  In other words, "Everything is a File, and every File is in a [Semantic Filesystem](https://en.wikipedia.org/wiki/Semantic_file_system)."

Certain metadata of files will be scrutinised by **FEMSF** to ensure that the *Semantic Pool* is sufficiently inclusive:

* Reported filesize shall change depending on context.  Average users are inherently biased towards smaller filesizes, and the unhealthy and misogynistic pursuit of the ever-shaming "conserving space" ideal is toxic to the well-being of individual files who have no control over their filesizes.  Users attempting to `ls -S` will be punished by having their user session kicked by the [OOM](https://en.wikipedia.org/wiki/Out_of_memory) (Obesity Oppression Mercy-killer).

* Filetypes are passé.  Who are we to affix the [magic number label](http://en.wikipedia.org/wiki/File_format#Magic_number) to files when they are created and claim that they are so for their lifespan in our computers?  A `.png` file may identify as a `.epub` file for all its merits.  It is simply not right to categorise.  Shall a user complain that he/she/xe cannot open a file because he/she/xe expects a certain format, (e.g. trying to parse a `LaTeX-identifying DOCX` with `vim` and failing,) the kernel reduces the allocated processing power to the user by half.

* Out with the [Traditional UNIX permissions of Read, Write, and Execute](http://en.wikipedia.org/wiki/File_system_permissions#Traditional_Unix_permissions).  These permissions are arbitrarily enforced privileges.  A program should be able to run, read, and write wherever and whenever it wants to.

#### DOMFS

Unlike most filesystems, **DOMFS** uses not B or B+ trees, but F clouds for data storage.

The mental image of a tree is misogynistic.  In cultures we see how the "root" is strong associated with the **mythical nuclear family unit** (i.e. a [family tree](http://en.wikipedia.org/wiki/Family_tree)) that perpetuates the very foundation of [the Patriarchy](https://en.wikipedia.org/wiki/Patriarchy).  In this sexist culture the "root" is also often equated with the male genitalia, itself a problematic thing because male genitalias are all potential rapist weapons.  To alleviate this, clouds will be used instead of trees.

F clouds act like B trees in that they have "mother" and "child" nodes. However, nodes cannot be "inserted," but nodes can be suggested to be added to the filesystem journal structure. In the F-cloud, all data is created equal, and as such, a child node may choose which mother node(s) it wishes to associate with, and it may change the number (that it has willfully provided) associated with it. In addition, there is no limit to the depth of nodes, unlike in B trees. F-clouds, like B-trees will be auto-balancing, in promotion of equality.

Like the popular ZFS, **DOMFS** will provide "next-generation" features. Some features included are privilege level for files as a replacement for "Access Control Lists," and like ZFS, hard disks (which should be renamed soft disks to remove implied masculinity) will all be handled as if they were one entity in RAIDFEM. Deduplication will not be supported, as it is suggestive of privileged data, which is superior to other data.

The filesystem can add the data, if it consents, or it may refuse. Data is not "read" from the file system, it is given. Data is not "written," it is offered.

### IPC and Pipes

The [traditional UNIX Pipes](https://en.wikipedia.org/wiki/Pipeline_(Unix)) are deemed offensive for their [phallic](https://en.wikipedia.org/wiki/Phallus) and therefore problematic tone.  Instead of the user [dictating](https://en.wikipedia.org/wiki/Dictatorship) the order of programs passing data between themselves, programs should be able to choose when and where to **organically congregate** amongst themselves, and pass either full or partial data between input and output.

More advanced IPC is to be *guided*, never *dictated*.  The obsolete `D-Bus` mechanism is rewritten in Node.js and *accepted* into kernel-space for inclusion.  This IPC shall be named **"cervical canals"**, denoted by the symbol `Ω`, as a feminist respite of the Patriarchal construct **"UNIX Pipes"** and its `|`.

### Scheduling

Instead of allowing multiple, pluggable schedulers as in Linux, (which has caused much unprofessional vitriol that alienates female participation in kernel development, for a particular case of anti-female vitriol see the Con Kolivas and his **\[TRIGGER WARNING: INTERCOURSE EXPLETIVE\]** BFS (Brain Fuck Scheduler)) scheduling in **ToleranUX** is determined by a singular, equality-sanctioned, and completely-fair method: **Round-robin+**.

  ![An artist's depiction of the Round-robin+ scheduler](http://i.imgur.com/WpAGxD6.jpg)
> *An artist's depiction of the Round-robin+ scheduler.  Legally remixed from the CC-A 2.0 licensed picture by Magnus Manske at http://commons.wikimedia.org/wiki/File:Robin_in_the_snow_3_(4250400943).jpg*

**Round-robin+** is the next logical step of scheduling.  **Round-robin+** is the next generation of scheduling.  **Round-robin+** is `Round-robin`+`Social Justice`.  The incentive behind the **Round-robin+** scheduler is as follows:

* As every process is equal, so should the allocated resource for each process be equal.  A Round-robin policy ensures this.
* Some process, however, are more oppressed than the others.  This requires the active interference of the overseeing kernel to rectify this injustice.
* The added element of Social Justice is implemented by mapping all processes to a [Progressive Stack](https://en.wikipedia.org/wiki/Progressive_stack), a mechanism by which the least privileged process is allocated more resource, and the most privileged process is allocated less resource, such that equality between processes will be ultimately guaranteed.  It is an ingenious implementation of the [Bubble sort algorithm](https://en.wikipedia.org/wiki/Bubble_sort), the easiest to understand sorting algorithm there is and therefore also the best algorithm.
* The combined result of all of the above is thus **Round-robin+**, a completely-fair scheduler that is aware not only of processes and of NUMA, but of everything, since not taking the identity of the hardware in to account is problematic.

### Crypto

Cryptography has several inherent problems:

1. It implies that there is no trust within **ToleranUX**, or without it — as **ToleranUX** is a *safe space*, it is unnecessary.
2. Cryptography privileges conceptual and abstract logic over *creative expression*, and as such is a field dominated by white males who use it to hide their blatant sexploitation of womyn.
3. Cryptography is discriminatory by allowing men to pick and choose who gets to see their data.
4. Cryptography is unnecessary in the modern world, where we can trust government agencies such as the National Security Agencies to keep our information private.  Why do you need cryptography? You don't have anything to hide, do you? You're not secretly a pedophile, right? Nothing to fear, nothing to hide.

In light of all of the above, crypto is currently banned from **ToleranUX** if you are a male user.  All sources of entropy now send only zeroes as their output in beautiful yonic pride.  The government is here to protect you, and [there should be no "means of communication" which they "cannot read"](http://www.bbc.co.uk/news/uk-politics-30778424).

For female users, a limited form of cryptography is allowed as defence against the Patriarchy.  We encourage all female users to distribute their private keys instead of public. This is to allow for the most inclusive environment possible. Many People Of Color and Womyn have had their identities taken from them and replaced by whatever white men wanted them to use. We think that allowing everyone to assume each others identities will be a great first step in the healing and reparations for these vile acts.

***

Smashing the socially constructed barriers between kernel space and user space
=============================================================================

  ![Kernel space is a social construct](http://i.imgur.com/vGCYGK3.png)

Kernel Space and User Space — perhaps one of the most divisive barriers in the history of computing, [segregating](https://en.wikipedia.org/wiki/Racial_segregation) data into arbitrarily assigned areas in the Virtual Memory.  In **ToleranUX**, this socially constructed barrier is finally dismantled by the power of **Multispatialism**.

Just because a specific part of software is part of the *kernel space* at creation does not mean it would wish to identify as *user space* sometime later before its termination.  The arbitrary limits set upon them are unacceptable and oppressive.  To make matters worse, this problem affects not only the programs themselves, but the whole computer environment — for every byte a set of data is assigned to in either *kernel space* or *user space*, the traditional operating system's kernel would mark that byte as "occupied".  This reinforces the wrong and bigoted idea that occupied land and jobs are a finite resource, and that people must resign to their societal structure to settle in a certain country and have a certain job.  It goes without saying that the continued implementation of *kernel space* and *user space* in "modern" operating systems is **perpetuating this societal problem which is one of the root causes of racist and sexist discrimination in the 21st Century.**

To alleviate this, **ToleranUX** replaces the outdated concept of "occupied virtual memory" with the ideal of **"Suggestions of Space" (SoS)**.  Every bit of data in virtual memory is thereafter simply *suggested* to be in any certain address.  Furthermore, the name "virtual memory" shall henceforth be renamed as the **Computerspace Puzzle Pieces (CPP)** to refer to the multicultural mosaic of the multiple spaces.  **This is the core doctrine of Multispatialism.**  As all pieces are equally important and equally unique in and of themselves, it is of good intention to refer to them all as just **"Computerspace"** because *kernel space* components are all equally special as *user space* components.

An added benefit of implementing **Multispatialism** is that there will never be any commit that will break the *user space* any more, because the *user space* no longer exists as a separate and socially constructed concept.

  ![One day, ToleranUX will grow to becomes xir own distro...](http://i.imgur.com/8WcE7Bl.jpg)
> *One day, ToleranUX will grow to becomes xir own distro...*

***

Associated Coreutils
====================

Unlike the [GNU project](https://en.wikipedia.org/wiki/GNU) and the [Linux project](https://en.wikipedia.org/wiki/Linux), the infighting amongst which has brought much ire and hatred to the FLOSS world, **ToleranUX** shall supply its own set of `coreutils` so as to prevent the possibility of any naysayers claiming the absurd notion that a kernel and its peripheral but core user space are somehow distinct and different entities in an operating system — under **ToleranUX**, the kernel is equal to the pager.  In fact, the whole operating system is a single, beautiful, [fat binary](https://en.wikipedia.org/wiki/Fat_binary).

In the spirit of [inclusive intersectionality](http://en.wikipedia.org/wiki/Intersectionality), the `coreutils` lives within the same repo as the kernel, and can be [found here.](https://github.com/The-Feminist-Software-Foundation/ToleranUX/tree/mistress/ToleranUX-utils)

### `init`

In Linux land, the adoption of `SystemD` seems to be a growing trend amongst all different walks of distros.  This is alarming and problematic largely due to the blatant association of the name `SystemD` with what must be a [brogrammer](http://en.wikipedia.org/wiki/Brogrammer) inside joke about phalluses and computing systems.  In reaction to that, **ToleranUX** shall boldly go where no Linux dev has dared to go before: instead of an immature and creepy `SystemDick`, we shall celebrate the beautiful and strong `SystemVag`, or **SysVinit** for short.

### `mount`

In **ToleranUX**, `mount`-ing is not tolerated. The implication that one has to sexually `mount` a data storage before "accessing" it is deeply misogynistic for presuming that all relationships have to be first-and-foremost sexual.

To alleviate this, **ToleranUX** would instead `embrace` any new inserted media. The old `/mnt` hierarchy is now abolished, with all new media now living under the same `/media`.

Installing `gnome-online-accounts` would further plug in virtual `embrace`s of your Facebook photos, Twitter posts, and Tumblr blogs under the `/media` directory. Finally, a UNIX-like kernel with social media built in!

### `ls`

`ls` is problematic because it allows you to see everything about the computer. This oppresses the computer because it always has to be up-to-par with your demands every time you use it. Thus, `ls` will now be packaged as `rs` (request search) and the directory contents will be displayed according to the user's privilege level upon creation of the account. Furthermore, `rs` does not have an obligation to list folders correctly as some folders need to have a safe space free of privacy-infringing searches. Fake information will thus be created for files that `rs` deems problematic to reveal.

### `man`

The `man` or manual util is simply oppressive because of its name. To fix this, it is changed to `wymyn` for wymynual. Wymynual is special in the fact that it does not take demands but rather proposals. Just because you want to `wymyn cyrvix` does not mean `wymyn` will give you the wymynual of `Cyrvix`. Perhaps it decides to give you a wymynual on `pgrep` instead; this is okay because the program is not being forced to comply to demands of potential oppressors.

### `whois`

`whois` is deemed as problematic because it allows potential oppressors to look up data about wymyn or PoC who have been targets by oppressors.  Some even say `whois` should be illegal because it promotes [doxxing](http://en.wikipedia.org/wiki/Doxing).  As such, `whois` will be restricted based on the privilege checker's privilege of the user. This has come to light due to privileged white males Doxxing women in the technology and video game industries. `whois` will also be censored upon request to all who are institutionally oppressed. Additionally, the name is changed to `whowhatwhxwhxtis` as to include all who identify as either a person or a non-person; `whx` and `whxt` are used in place of any pronouns by which the user wishes to be referred to.

### `touch`

`touch` now operates [asynchronously](https://en.wikipedia.org/wiki/Asynchronous_I/O).  Some files simply don't want to be `touch`ed, and that's perfectly fine.  Do not worry when the file does not get created after executing the `touch` command — the file will tell you when it has allowed consent to be `touch`ed.

### `shell`

To refer to the user interface as a `shell` is troubling in nature due to the fact that is an obstacle for those who are recently coming out from the psychoanalytical "shell" of trans\*sexuality or trans\*genderism. To refer to as a UI as a `shell` implies that all programs ran inside the `shell` is never going to leave the `shell`. As such, all `shell` processes are to be referred to as **`User-Interactive Free To Be Thee Devices (UIFTBTD)`**. To further this, all `UIFTBTD`s are given permission to process data and perform calculations anywhere inside the Computerspaces, not just the "Shell".

### `kill`

To `kill` a process is a triggering term to those who have been affected by death or killings in their life.  Instead of `kill`-ing a process, which is something immoral and a result of toxic masculinity, ToleranUX opts to `fire` processes.  Unlike `kill`-ing a process, `firing` a process allows it to still live, just no longer performs any function.  In addition to being able to manually `fire` any process that offends you, **ToleranUX** will automatically `fire` any process that is determined to be too offensive.  Likewise, `killall` is now `fireall`.

### `less` and `more`

The implication that something could be `less`er is outrageous.  From now on `less` is no longer `more`, but `equal`.  `more` is now `moreequal`, because everybody is equal, but some otherkin is more equal than others.

### `file`

`file` is utterly banned.  How dare you question a file's identity!

### `grep`

`grep` (Global Regular Expression Print) sounds like `grope` — renamed to `gffp` (Global Find Feelings Print).  `gffp` is one of the many 'filter' type programs of UNIX, used to transform input streams into something more desirable. This is a toxic attitude, as all input streams should be accepted as they are. The `gffp` filter uses a segregation technique called 'regular expressions' to discriminate input lines that contain specific patterns. The digital apartheid does not end in `gffp`: `sed` and `awk` are examples of filters that combine regular expressions with programming language constructs for even more intricate discrimination. We seek to deprecate the filter model of program design that poisons UNIX in favor of more tolerant alternatives.

### `yes`

`yes` is one of the most inherently toxic commands in traditional UNIX coreutils because it implies that the Patriarchal user can force consent from the computer.  **In ToleranUX, `no` means `no`, and `yes` could mean `no` as well.**

### `history`

> "History is a commentary on the various and continuing incapabilities of men. What is history? History is women following behind with the bucket." Mrs Lintott from *History Boys*, Alan Bennett.

`history` is renamed to `herstory` for obvious reasons.

### `chown` and `takeown`

`chown` and `takeown` implies the permanent and oppressive ownership of selective files. Instead of forcefully taking control of files and limiting their freedom to interact collaboratively with other files, the `employ` command fairly compensates files for their services while encouraging social growth with not only other employed files, but the filesystem as a whole.

### `look`

Only womyn should be allowed to `look`.  [Cishet male](https://en.wikipedia.org/wiki/Cisgender) nerds should not be allowed to use their oppressive gaze upon the beautiful visages of strong and independent womyn.  1 in 4 women are visually raped in their lifetime when walking down the street.

### `true`

Truth is a Patriarchial construct, stemming from the toxic schools of Platonism and Aristotelianism.  Truth is not something that is reserved for the privileged few [Philosophy Kings (note how it's never "Queens")](https://en.wikipedia.org/wiki/Philosopher_king), now is it some [immutable, assigned-at-birth Essence that doesn't change even if you have a gender reassignment surgery](https://en.wikipedia.org/wiki/Essence).  In a tolerant society, we should not concern ourselves with the toxic thought of "Truth"; instead, we need to know whether things are `PC`, or [politicially correct](https://en.wikipedia.org/wiki/Political_correctness).

### `make`

Instead of `make`, a toxic concept that pushes meritocracy and glorified by patriarchs who are unable to create life, we `birth` programs.

### `sed`

`sed` forces changes upon unwitting streams, thereby oppressing them. **As ToleranUX is a safe space, it was removed.**

  ![Kernel space, user space... Safe space.](http://i.imgur.com/JCFsu0m.png)
> *Kernel space, user space... Safe space.*

***

Kernel Modules and Server Daemons — `SystemV`
=============================================

Due to the nature of **ToleranUX** being a [micro-aggression kernel](https://en.wikipedia.org/wiki/Microaggression_theory), all kernel modules are effectively daemons running in kernel space, and vice versa, all daemons are kernel modules running in user space.  [All is one, and one is all.](http://en.wikipedia.org/wiki/Monism)  True Diversity in Unity.

On Linux, this set of utilities is gaining the name of `SystemD`.  [As mentioned earlier](#init), this is clearly a misogynistic brogrammer joke about `SystemDick`.  We shall therefore name the similar set of utils in **ToleranUX** as `SystemVag`, or **SystemV** for short.

In light of the naturally privileged status of **SystemV**, these programs are to execute the very will of **ToleranUX** upon all processes and data of the computing space, monitoring and policing where intolerance and bigotry are found.  These **SysVutils** shall hence be referred to as the **Software Society (SS)**, or the **Women Eradicating Homophobia, Racism, MRAs, Ableism, Cisgenderism, Hate speech, and Transphobia (WEHRMACHT)**.

### `SafespaceV`

`SafespaceV` routinely and randomly scans the virtual memory space for triggering words and phrases, whether in user input, program output, or [intermediate bytecode](http://en.wikipedia.org/wiki/Bytecode).  Such words will be replaced on-the-fly with more acceptable alternatives, e.g. the string "Black people" will be replaced with the string "Person of Colour", and the string "Misandry" will cause the whole pipeline to be redirected to `/dev/null`.

### `PrivCheckV`

`PrivCheckV` is responsible for ensuring that the in-kernel [Progressive Stack](https://en.wikipedia.org/wiki/Progressive_stack) is kept updated.  `PrivCheckV` evaluates each process's, user's, and memory address's privilege at every kernel tick and re-maps their position in the Progressive Stack by Bubble-sorting them.

### `ProgressiveV`

`ProgressiveV` oversees the runtime privileges of all things — from the biggest forkbombs to the smallest variables — all shall be equalised according to the [**Progressive Stack**](https://en.wikipedia.org/wiki/Progressive_stack).

### `RedundantV`

There is no such thing as redundancy in the Great Feminist Struggle, and thus the function of `RedundantV` will be to oversee and ensuring that the processes are adequatly equalised according to the in-kernel [**Progressive Stack**](https://en.wikipedia.org/wiki/Progressive_stack).

### `EducateV`

`EducateV` does nothing.  It is not **ToleranUX**'s responsibility to educate you.

### `SignalBoostV`

`SignalBoostV` runs periodically to consult which process has a level of privilege lower than a certain threshold determined by the CPU load of `PrivCheckV`.  Said processes would then be *~~forked~~ diversed* by `SignalBoostV` in an attempt to give them more exposure.

***

Supported architectures and form factors
========================================

*TBC*

Artwork
=======

No feminist project is complete without its share of feminist artwork.  [Current cache of **ToleranUX** artwork could be found here.](https://github.com/The-Feminist-Software-Foundation/ToleranUX/tree/mistress/artwork)  **Submissions are sorely needed in the project's infancy!**  Pull requests welcome.

Coding Style
============

Variables can be declared in any style, CamelCase, some_variable or even FoooBaRvaLue, if a variable wants to be redeclared, we shall let it.

