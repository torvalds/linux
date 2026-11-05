.. SPDX-License-Identifier: GPL-2.0

Regras sobre como acessar informações no sysfs
==============================================

O sysfs exportado pelo kernel expõe detalhes internos de implementação do
kernel e depende de estruturas e do leiaute internos do kernel. Há um consenso
entre os desenvolvedores do kernel de que o kernel Linux não fornece uma API
interna estável. Portanto, existem aspectos da interface do sysfs que podem
não ser estáveis entre lançamentos do kernel.

Para minimizar o risco de quebrar os usuários do sysfs — que, na maioria dos
casos, são aplicações de espaço de usuário de baixo nível — a cada novo
lançamento do kernel, os usuários do sysfs devem seguir algumas regras para usar
uma forma o mais abstrata possível de acessar esse sistema de arquivos. Os
programas udev e HAL atuais já implementam isso, e os usuários são incentivados
a se conectar, se possível, às abstrações que esses programas fornecem em vez de
acessar o sysfs diretamente.

Mas, se você realmente quer ou precisa acessar o sysfs diretamente, siga as
seguintes regras e, assim, seus programas deverão funcionar com versões
futuras da interface do sysfs.

- Não use a libsysfs
    Ela faz suposições sobre o sysfs que não são verdadeiras. Sua API não
    oferece nenhuma abstração; ela expõe todos os detalhes de implementação do
    driver-core do kernel em sua própria API. Portanto, ela não é melhor do que
    ler diretórios e abrir os próprios arquivos.
    Além disso, ela não é mantida ativamente no sentido de refletir o
    desenvolvimento atual do kernel. O objetivo de fornecer uma interface
    estável para o sysfs falhou; ela causa mais problemas do que resolve. Ela
    viola muitas das regras deste documento.

- sysfs está sempre em ``/sys``
    Analisar ``/proc/mounts`` é perda de tempo. Outros pontos de montagem são um
    bug de configuração do sistema que você não deve tentar contornar. Para
    casos de teste, talvez ofereça suporte a uma variável de ambiente
    ``SYSFS_PATH`` para sobrescrever o comportamento da aplicação, mas nunca
    tente procurar pelo sysfs. Nunca tente montá-lo, a menos que você seja um
    script de inicialização inicial (early boot).

- Dispositivos são apenas "dispositivos"
    Não existe algo como dispositivos de classe, de barramento, físicos,
    interfaces e afins com os quais você possa contar no espaço de usuário.
    Tudo é simplesmente um "dispositivo". Tipos como classe, barramento,
    físico... são apenas detalhes de implementação do kernel que não devem ser
    esperados por aplicações que procuram dispositivos no sysfs.

    As propriedades de um dispositivo são:

    - devpath (``/devices/pci0000:00/0000:00:1d.1/usb2/2-2/2-2:1.0``)

      - idêntico ao valor de DEVPATH no evento enviado pelo kernel na criação e
        remoção do dispositivo
      - a chave única para o dispositivo naquele momento
      - o caminho do kernel para o diretório do dispositivo sem o ``/sys`` inicial,
        e sempre começando com uma barra
      - todos os elementos de um devpath devem ser diretórios reais. Links
        simbólicos apontando para /sys/devices devem ser sempre resolvidos para o
        seu alvo real e o caminho do alvo deve ser usado para acessar o dispositivo.
        Dessa forma, o devpath para o dispositivo corresponde ao devpath do kernel
        usado no momento do evento.
      - usar ou expor valores de symlinks como elementos em uma string de devpath
        é um bug na aplicação

    - nome do kernel (``sda``, ``tty``, ``0000:00:1f.2``, ...)

      - um nome de diretório, idêntico ao último elemento do devpath
      - as aplicações precisam lidar com espaços e caracteres como ``!`` no
        nome

    - subsistema (``block``, ``tty``, ``pci``, ...)

      - string simples, nunca um caminho ou um link
      - recuperado através da leitura do link "subsystem" e usando apenas o
        último elemento do caminho de destino

    - driver (``tg3``, ``ata_piix``, ``uhci_hcd``)

      - uma string simples, que pode conter espaços, nunca um caminho ou um
        link
      - é recuperado pela leitura do link "driver" e usando apenas o último
        elemento do caminho de destino
      - dispositivos que não possuem o link "driver" simplesmente não possuem um
        driver; copiar o valor do driver no contexto de um dispositivo filho é
        um bug na aplicação

    - atributos

      - os arquivos no diretório do dispositivo ou arquivos abaixo de subdiretórios
        do mesmo diretório de dispositivo
      - acessar atributos alcançados por um symlink apontando para outro dispositivo,
        como o link "device", é um bug na aplicação

    Tudo o mais é apenas um detalhe de implementação do driver-core do kernel
    que não deve ser presumido como estável entre lançamentos do kernel.

- Propriedades de dispositivos pais nunca pertencem a um dispositivo filho.
    Sempre olhe para os próprios dispositivos pais para determinar as propriedades
    de contexto do dispositivo. Se o dispositivo ``eth0`` ou ``sda`` não tiver
    um link "driver", então esse dispositivo não possui um driver. Seu valor é vazio.
    Nunca copie nenhuma propriedade do dispositivo pai para um dispositivo filho.
    As propriedades do dispositivo pai podem mudar dinamicamente sem qualquer aviso
    para o dispositivo filho.

- Hierarquia em uma única árvore de dispositivos
    Há apenas um lugar válido no sysfs onde a hierarquia pode ser examinada
    e este é abaixo de: ``/sys/devices.``
    Está planejado que todos os diretórios de dispositivos terminarão na árvore
    abaixo deste diretório.

- Classificação por subsistema
    Atualmente, existem três locais para classificação de dispositivos:
    ``/sys/block,`` ``/sys/class`` e ``/sys/bus.`` Está planejado que estes não
    conterão diretórios de dispositivos em si, mas apenas listas planas de
    symlinks apontando para a árvore unificada ``/sys/devices``.
    Todos os três locais têm regras completamente diferentes sobre como acessar
    informações de dispositivos. Está planejado fundir todos os três diretórios de
    classificação em um único local em ``/sys/subsystem``, seguindo o leiaute dos
    diretórios de barramentos. Todos os barramentos e classes, incluindo o
    subsistema de blocos convertido, aparecerão lá.
    Os dispositivos pertencentes a um subsistema criarão um symlink no diretório
    "devices" em ``/sys/subsystem/<name>/devices``,

    Se ``/sys/subsystem`` existir, ``/sys/bus``, ``/sys/class`` e ``/sys/block``
    podem ser ignorados. Se ele não existir, você sempre terá que varrer todos os
    três locais, já que o kernel é livre para mover um subsistema de um local para
    o outro, desde que os dispositivos ainda sejam acessíveis pelo mesmo nome de
    subsistema.

    Presumir que ``/sys/class/<subsystem>`` e ``/sys/bus/<subsystem>``, ou
    ``/sys/block`` e ``/sys/class/block`` não são intercambiáveis é um bug na
    aplicação.

- Bloco
    O subsistema de blocos convertido em ``/sys/class/block`` ou
    ``/sys/subsystem/block`` conterá os links para discos e partições no mesmo
    nível, nunca em uma hierarquia. Presumir que o subsistema de blocos contém
    apenas discos e não dispositivos de partição na mesma lista plana é um bug
    na aplicação.

- Link "device" e links <subsystem>:<kernel name>
    Nunca dependa do link "device". O link "device" é uma solução alternativa
    para o leiaute antigo, onde dispositivos de classe não são criados em
    ``/sys/devices/`` como os dispositivos de barramento. Se a resolução de links
    de um diretório de dispositivo não terminar em ``/sys/devices/``, você pode
    usar o link "device" para encontrar os dispositivos pais em ``/sys/devices/``;
    esse é o único uso válido do link "device"; ele nunca deve aparecer em nenhum
    caminho como um elemento. Presumir a existência do link "device" para um
    dispositivo em ``/sys/devices/`` é um bug na aplicação.
    Acessar ``/sys/class/net/eth0/device`` é um bug na aplicação.

    Nunca dependa dos links específicos de classe de volta para o diretório
    ``/sys/class``. Esses links também são uma solução paliativa para o erro de
    projeto no qual dispositivos de classe não são criados em ``/sys/devices.``
    Se um diretório de dispositivo não contiver diretórios para dispositivos
    filhos, esses links podem ser usados para encontrar os dispositivos filhos em
    ``/sys/class.`` Esse é o único uso válido desses links; eles nunca devem
    aparecer em nenhum caminho como um elemento. Presumir a existência desses links
    para dispositivos que são diretórios reais de dispositivos filhos na árvore
    ``/sys/devices`` é um bug na aplicação.

    Está planejado remover todos esses links quando todos os diretórios de
    dispositivos de classe residirem em ``/sys/devices.``

- A posição dos dispositivos ao longo da cadeia de dispositivos pode mudar.
    Nunca dependa de uma posição específica do dispositivo pai no devpath,
    ou da cadeia de dispositivos pais. O kernel é livre para inserir dispositivos
    na cadeia. Você deve sempre requisitar o dispositivo pai que procura pelo
    valor do seu subsistema. Você precisa percorrer a cadeia para cima até
    encontrar o dispositivo correspondente ao subsistema esperado. Depender de
    uma posição específica de um dispositivo pai ou expor caminhos relativos
    usando ``../`` para acessar a cadeia de pais é um bug na aplicação.

- Ao ler e gravar arquivos de atributos de dispositivo do sysfs, evite a dependência
    de códigos de erro específicos sempre que possível. Isso minimiza o acoplamento
    com a implementação do tratamento de erros dentro do kernel.

    Em geral, falhas ao ler ou gravar atributos de dispositivos no sysfs devem
    propagar erros sempre que possível. Erros comuns incluem, mas não estão
    limitados a:

	``-EIO``: A operação de leitura ou gravação não é suportada, tipicamente
	retornada pelo próprio sistema sysfs se o ponteiro de leitura ou gravação
	for ``NULL``.

	``-ENXIO``: A operação de leitura ou gravação falhou

    Os códigos de erro não serão alterados sem um bom motivo, e caso uma alteração
    nos códigos de erro resulte em quebra no espaço de usuário, ela será corrigida,
    ou a alteração causadora será revertida.

    As aplicações do espaço de usuário podem, no entanto, esperar que o formato e
    o conteúdo dos arquivos de atributos permaneçam consistentes na ausência de uma
    mudança no atributo de versão no contexto de um determinado atributo.
