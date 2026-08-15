.. SPDX-License-Identifier: GPL-2.0

Índice de Documentação Adicional do Kernel
==========================================

A necessidade de um documento como este tornou-se evidente na lista de discussão
linux-kernel, uma vez que as mesmas perguntas, solicitando referências de
informações, apareciam repetidamente.

Felizmente, à medida que cada vez mais pessoas chegam ao GNU/Linux, mais pessoas
se interessam pelo Kernel. No entanto, ler o código-fonte nem sempre é o
suficiente. É fácil entender o código, mas perder os conceitos, a filosofia
e as decisões de design por trás dele.

Infelizmente, não há muitos documentos disponíveis para iniciantes começarem.
E, mesmo quando existem, não havia um local "bem conhecido" que os centralizasse.
Estas linhas tentam suprir essa falta.

POR FAVOR, se você conhece algum artigo não listado aqui ou se escrever um novo
documento, inclua uma referência a ele aqui, seguindo o processo de envio de
patches do kernel. Quaisquer correções, ideias ou comentários também são
bem-vindos.

Todos os documentos estão catalogados com os seguintes campos: o "Título" do
documento, o(s) "Autor(es)", a "URL" onde podem ser encontrados, algumas
"Palavras-chave" úteis para pesquisar tópicos específicos e uma breve
"Descrição" do documento.

.. note::

   Os documentos em cada seção deste documento estão ordenados por sua data de
   publicação, do mais recente para o mais antigo. O(s) mantenedor(es) deve(m)
   remover periodicamente recursos à medida que se tornem obsoletos ou
   desatualizados; com exceção de livros fundamentais.

Documentação na árvore do Kernel
--------------------------------

Os manuais Sphinx devem ser compilados com ``make {htmldocs | pdfdocs | epubdocs}``.

    * Nome: **linux/Documentation**

      :Autor: Muitos.
      :Localização: Documentation/
      :Palavras-chave: arquivos de texto, Sphinx.
      :Descrição: Documentação que acompanha o código-fonte do kernel,
        dentro do diretório Documentation. Algumas páginas deste documento
        (incluindo este próprio documento) foram movidas para lá e podem
        estar mais atualizadas do que a versão web.

Documentação on-line
--------------------

    * Título: **Linux Kernel Mailing List Glossary**

      :Autor: diversos
      :URL: https://kernelnewbies.org/KernelGlossary
      :Data: versão contínua (rolling)
      :Palavras-chave: glossário, termos, linux-kernel.
      :Descrição: Da introdução: "Este glossário destina-se a ser uma breve
        descrição de algumas das siglas e termos que você poderá ouvir durante
        as discussões sobre o kernel Linux".

    * Título: **The Linux Kernel Module Programming Guide**

      :Autor: Peter Jay Salzman, Michael Burian, Ori Pomerantz, Bob Mottram,
        Jim Huang.
      :URL: https://sysprog21.github.io/lkmpg/
      :Data: 2021
      :Palavras-chave: módulos, livro GPL, /proc, ioctls, chamadas de sistema,
        manipuladores de interrupção.
      :Descrição: Um excelente livro sob licença GPL sobre o tópico de
        programação de módulos. Repleto de exemplos. Atualmente, a nova versão
        está sendo mantida ativamente em https://github.com/sysprog21/lkmpg.

Livros Publicados
-----------------

    * Title: **The Linux Memory Manager**

      :Autor: Lorenzo Stoakes
      :Editora: No Starch Press
      :Data: Fevereiro 2025
      :Páginas: 1300
      :ISBN: 978-1718504462
      :Notas: Gerenciamento de memória. Rascunho completo disponível como acesso
        antecipado para ré-venda, lançamento completo agendado para o
        outono de 2025. Veja https://nostarch.com/linux-memory-manager
        para mais informações.

    * Title: **Practical Linux System Administration: A Guide to Installation, Configuration, and Management, 1st Edition**

      :Autor: Kenneth Hess
      :Editora: O'Reilly Media
      :Data: Maio, 2023
      :Páginas: 246
      :ISBN: 978-1098109035
      :Notas: Administração de sistemas

    * Title: **Linux Kernel Debugging: Leverage proven tools and advanced techniques to effectively debug Linux kernels and kernel modules**

      :Autor: Kaiwan N Billimoria
      :Editora: Packt Publishing Ltd
      :Data: Agosto, 2022
      :Páginas: 638
      :ISBN: 978-1801075039
      :Notas: Livro sobre depuração (debugging)

    * Title: **Linux Kernel Programming: A Comprehensive Guide to Kernel Internals, Writing Kernel Modules, and Kernel Synchronization**

      :Autor: Kaiwan N Billimoria
      :Editora: Packt Publishing Ltd
      :Data: Março, 2021 (Segunda edição publicada em 2024)
      :Páginas: 754
      :ISBN: 978-1789953435 (O ISBN da segunda edição é 978-1803232225)

    * Title: **Linux Kernel Programming Part 2 - Char Device Drivers and Kernel Synchronization: Create user-kernel interfaces, work with peripheral I/O, and handle hardware interrupts**

      :Autor: Kaiwan N Billimoria
      :Editora: Packt Publishing Ltd
      :Data: Março, 2021
      :Páginas: 452
      :ISBN: 978-1801079518

    * Title: **Linux System Programming: Talking Directly to the Kernel and C Library**

      :Autor: Robert Love
      :Editora: O'Reilly Media
      :Data: Junho, 2013
      :Páginas: 456
      :ISBN: 978-1449339531
      :Notas: Livro fundamental

    * Título: **Linux Kernel Development, 3rd Edition**

      :Autor: Robert Love
      :Editora: Addison-Wesley
      :Data: Julho de 2010
      :Páginas: 440
      :ISBN: 978-0672329463
      :Notas: Livro fundamental

    * Título: **Linux Device Drivers, 3rd Edition**

      :Autores: Jonathan Corbet, Alessandro Rubini e Greg Kroah-Hartman
      :Editora: O'Reilly & Associates
      :Data: 2005
      :Páginas: 636
      :ISBN: 0-596-00590-3
      :Notas: Livro fundamental. Mais informações em
        http://www.oreilly.com/catalog/linuxdrive3/
        Formato PDF, URL: https://lwn.net/Kernel/LDD3/

    * Título: **The Design of the UNIX Operating System**

      :Autor: Maurice J. Bach
      :Editora: Prentice Hall
      :Data: 1986
      :Páginas: 471
      :ISBN: 0-13-201757-1
      :Notas: Livro fundamental

Diversos
--------

    * Nome: **Cross-Referencing Linux**

      :URL: https://elixir.bootlin.com/
      :Palavras-chave: Navegação em código-fonte.
      :Descrição: Outro navegador web para o código-fonte do kernel Linux.
        Possui muitas referências cruzadas para variáveis e funções. Você pode
        ver onde elas são definidas e onde são utilizadas.

    * Nome: **Linux Weekly News**

      :URL: https://lwn.net
      :Palavras-chave: últimas notícias do kernel.
      :Descrição: O título diz tudo. Há uma seção fixa sobre o kernel que
        resume o trabalho dos desenvolvedores, correções de bugs, novos recursos
        e versões produzidas durante a semana.

    * Nome: **The home page of Linux-MM**

      :Autor: A equipe Linux-MM.
      :URL: https://linux-mm.org/
      :Palavras-chave: gerenciamento de memória, Linux-MM, mm patches, TODO,
        docs, mailing list.
      :Descrição: Site dedicado ao desenvolvimento do Gerenciamento de Memória
        do Linux. Patches relacionados à memória, HOWTOs, links, desenvolvedores
        mm... Não perca se você estiver interessado no desenvolvimento do
        gerenciamento de memória!

    * Nome: **Kernel Newbies IRC Channel and Website**

      :URL: https://www.kernelnewbies.org
      :Palavras-chave: IRC, novatos, canal, tirar dúvidas.
      :Descrição: #kernelnewbies em irc.oftc.net.
        O canal #kernelnewbies é uma rede de IRC dedicada ao hacker de kernel
        "novato" (newbie). O público consiste principalmente de pessoas que estão
        aprendendo sobre o kernel, trabalhando em projetos do kernel ou hackers
        profissionais que desejam ajudar pessoas menos experientes.
        O #kernelnewbies está na rede de IRC OFTC.
        Tente acessar irc.oftc.net como seu servidor e então digite /join #kernelnewbies.
        O site kernelnewbies também hospeda artigos, documentos, FAQs...

    * Nome: **linux-kernel mailing list archives and search engines**

      :URL: https://subspace.kernel.org
      :URL: https://lore.kernel.org
      :Palavras-chave: linux-kernel, arquivos, busca.
      :Descrição: Alguns dos arquivadores da lista de discussão linux-kernel.
        Se você conhece algum outro (ou um melhor), por favor, me avise.

    * Nome: **The Linux Foundation YouTube channel**

      :URL: https://www.youtube.com/user/thelinuxfoundation
      :Palavras-chave: linux, vídeos, linux-foundation, youtube.
      :Descrição: A Linux Foundation faz o upload de gravações de vídeo de seus
        eventos colaborativos, conferências de Linux (incluindo a LinuxCon) e
        outras pesquisas originais e conteúdos relacionados ao Linux e ao
        desenvolvimento de software.

Rust
----

    * Título: **Rust for Linux**

      :Autor: diversos
      :URL: https://rust-for-linux.com/
      :Data: versão contínua (rolling)
      :Palavras-chave: glossário, termos, linux-kernel, rust.
      :Descrição Do site: "Rust for Linux é o projeto que adiciona suporte à
        linguagem Rust ao kernel Linux. Este site pretende ser um hub de links,
        documentação e recursos relacionados ao projeto".

    * Título: **Learn Rust the Dangerous Way**

      :Autor: Cliff L. Biffle
      :URL: https://cliffle.com/p/dangerust/
      :Data: Acessado em 11 de setembro de 2024
      :Palavras-chave: rust, blog.
      :Descrição: Do site: "LRtDW é uma série de artigos que coloca os recursos
        do Rust em contexto para programadores C de baixo nível que talvez não
        tenham uma formação formal em Ciência da Computação, o tipo de pessoa
        que trabalha com firmware, engines de jogos, kernels de SO e afins.
        Basicamente, pessoas como eu.". O site ilustra conversões de linha por
        linha de C para Rust.

    * Título: **The Rust Book**

      :Autor: Steve Klabnik e Carol Nichols, com contribuições da comunidade Rust
      :URL: https://doc.rust-lang.org/book/
      :Data: Acessado em 11 de setembro de 2024
      :Palavras-chave: rust, livro.
      :Descrição: Do site: "Este livro abraça totalmente o potencial do Rust para
        capacitar seus usuários. É um texto amigável e acessível destinado a
        ajudá-lo a elevar não apenas seu conhecimento de Rust, mas também seu
        alcance e confiança como programador em geral. Então mergulhe de cabeça,
        prepare-se para aprender e bem-vindo à comunidade Rust!".

    * Título: **Rust for the Polyglot Programmer**

      :Autor: Ian Jackson
      :URL: https://www.chiark.greenend.org.uk/~ianmdlvl/rust-polyglot/index.html
      :Data: Dezembro de 2022
      :Palavras-chave: rust, blog, tooling.
      :Descrição: Do site: "Existem muitos guias e introduções ao Rust. Este é
        algo diferente: destina-se ao programador experiente que já conhece
        muitas outras linguagens de programação. Tento ser abrangente o suficiente
        para servir de ponto de partida para qualquer área do Rust, mas evito
        entrar em detalhes excessivos, exceto onde as coisas não são como você
        poderia esperar. Além disso, este guia não é inteiramente isento de
        opiniões, incluindo recomendações de bibliotecas (crates), ferramentas, etc.".

    * Título: **Fasterthanli.me**

      :Autor: Amos Wenger
      :URL: https://fasterthanli.me/
      :Data: Acessado em 11 de setembro de 2024
      :Palavras-chave: rust, blog, notícias.
      :Descrição: Do site: "Eu crio artigos e vídeos sobre como os computadores
        funcionam. Meu conteúdo é de formato longo, didático e exploratório
        e frequentemente uma desculpa para ensinar Rust!".

    * Título: **Comprehensive Rust**

      :Autor: Equipe Android do Google
      :URL: https://google.github.io/comprehensive-rust/
      :Data: Acessado em 13 de setembro de 2024
      :Palavras-chave: rust, blog.
      :Descrição: Do site: "O curso cobre todo o espectro do Rust, desde a
        sintaxe básica até tópicos avançados como genéricos e tratamento de erros".

    * Título: **The Embedded Rust Book**

      :Autor: Múltiplos colaboradores, principalmente Jorge Aparicio
      :URL: https://docs.rust-embedded.org/book/
      :Data: Acessado em 13 de setembro de 2024
      :Palavras-chave: rust, blog.
      :Descrição: Do site: "Um livro introdutório sobre o uso da linguagem de
        programação Rust em sistemas embarcados 'Bare Metal', como microcontroladores".

    * Título: **Experiment: Improving the Rust Book**

      :Autor: Cognitive Engineering Lab na Brown University
      :URL: https://rust-book.cs.brown.edu/
      :Data: Acessado em 22 de setembro de 2024
      :Palavras-chave: rust, blog.
      :Descrição: Do site: "O objetivo deste experimento é avaliar e melhorar o
        conteúdo do Rust Book para ajudar as pessoas a aprenderem Rust de forma
        mais eficaz".

    * Título: **New Rustacean** (podcast)

      :Autor: Chris Krycho
      :URL: https://newrustacean.com/
      :Data: Acessado em 22 de setembro de 2024
      :Palavras-chave: rust, podcast.
      :Descrição: Do site: "Este é um podcast sobre aprender a linguagem de
        programação Rust do zero! Além desta página inicial elegante, todo o
        conteúdo do site é construído com as próprias ferramentas de documentação
        do Rust".

    * Título: **Opsem-team** (repositório)

      :Autor: Equipe de semântica operacional (Operational semantics team)
      :URL: https://github.com/rust-lang/opsem-team/tree/main
      :Data: Acessado em 22 de setembro de 2024
      :Palavras-chave: rust, repositório.
      :Descrição: Do README: "A equipe opsem é a sucessora do grupo de trabalho
        unsafe-code-guidelines e é responsável por responder a muitas das perguntas
        difíceis sobre a semântica do Rust inseguro (unsafe Rust)".

    * Título: **You Can't Spell Trust Without Rust**

      :Autor: Alexis Beingessner
      :URL: https://repository.library.carleton.ca/downloads/1j92g820w?locale=en
      :Data: 2015
      :Palavras-chave: rust, mestrado, tese.
      :Descrição: Esta tese foca no sistema de propriedade (ownership) do Rust,
        que garante a segurança de memória ao controlar a manipulação de dados e
        o tempo de vida, enquanto também destaca suas limitações e o compara a
        sistemas semelhantes no Cyclone e C++.

    * Nome: **Apresentações de Rust no Linux Plumbers (LPC) 2024**

      :Título: Rust microconference
      :URL: https://lpc.events/event/18/sessions/186/#20240918
      :Título: Rust for Linux
      :URL: https://lpc.events/event/18/contributions/1912/
      :Título: Journey of a C kernel engineer starting a Rust driver project
      :URL: https://lpc.events/event/18/contributions/1911/
      :Título: Crafting a Linux kernel scheduler that runs in user-space using Rust
      :URL: https://lpc.events/event/18/contributions/1723/
      :Título: openHCL: A Linux and Rust based paravisor
      :URL: https://lpc.events/event/18/contributions/1956/
      :Palavras-chave: rust, lpc, apresentações.
      :Descrição: Uma série de palestras do LPC relacionadas ao Rust.

    * Nome: **The Rustacean Station Podcast**

      :URL: https://rustacean-station.org/
      :Palavras-chave: rust, podcasts.
      :Descrição: Um projeto comunitário para a criação de conteúdo em podcast
        sobre a linguagem de programação Rust.

-------

Este documento foi originalmente baseado em:

https://www.dit.upm.es/~jmseyas/linux/kernel/hackers-docs.html

e escrito por Juan-Mariano de Goyeneche.
